/*************************************************************************/
/*  fbx_material.cpp 				                                     */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2020 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2020 Godot Engine contributors (cf. AUTHORS.md)    */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "fbx_material.h"

#include "drivers/png/png_driver_common.h"
#include "modules/jpg/image_loader_jpegd.h"
#include "scene/resources/material.h"
#include "scene/resources/texture.h"

String FBXMaterial::get_material_name() const {
	return material_name;
}

void FBXMaterial::set_imported_material(const Assimp::FBX::Material *p_material) {
	material = p_material;
}

void FBXMaterial::add_search_string(String p_filename, String p_current_directory, String search_directory, Vector<String> &texture_search_paths) {
	if (search_directory.empty()) {
		texture_search_paths.push_back(p_current_directory.get_base_dir().plus_file(p_filename));
	} else {
		texture_search_paths.push_back(p_current_directory.get_base_dir().plus_file(search_directory + "/" + p_filename));
		texture_search_paths.push_back(p_current_directory.get_base_dir().plus_file("../" + search_directory + "/" + p_filename));
	}
}

// fbx will not give us good path information and let's not regex them to fix them
// no relative paths are in fbx generally they have a rel field but it's populated incorrectly by the SDK.
String FBXMaterial::find_texture_path_by_filename(const String p_filename, const String p_current_directory) {
	_Directory dir;
	Vector<String> paths;
	add_search_string(p_filename, p_current_directory, "", paths);
	add_search_string(p_filename, p_current_directory, "texture", paths);
	add_search_string(p_filename, p_current_directory, "textures", paths);
	add_search_string(p_filename, p_current_directory, "materials", paths);
	add_search_string(p_filename, p_current_directory, "mats", paths);
	add_search_string(p_filename, p_current_directory, "pictures", paths);
	add_search_string(p_filename, p_current_directory, "images", paths);

	for (int i = 0; i < paths.size(); i++) {
		if (dir.file_exists(paths[i])) {
			return paths[i];
		}
	}

	return "";
}

Vector<Ref<FBXMaterial::TextureFileMapping> > FBXMaterial::extract_texture_mappings(const Assimp::FBX::Material *material) const {
	Vector<Ref<TextureFileMapping> > mappings = Vector<Ref<TextureFileMapping> >();

	// TODO Layered textures are a collection on textures stored into an array.
	// Extract layered textures is not yet supported. Per each texture in the
	// layered texture array you want to use the below method to extract those.

	for (std::pair<std::string, const Assimp::FBX::Texture *> texture : material->Textures()) {
		const std::string &fbx_mapping_name = texture.first;

		ERR_CONTINUE_MSG(fbx_mapping_paths.count(fbx_mapping_name) <= 0, "This FBX has a material with mapping name: " + String(fbx_mapping_name.c_str()) + " which is not yet supported by this importer. Consider open an issue so we can support it.");

		const String absoulte_fbx_file_path = texture.second->FileName().c_str();
		const String file_extension = absoulte_fbx_file_path.get_extension();
		ERR_CONTINUE_MSG(
				file_extension != "png" &&
						file_extension != "PNG" &&
						file_extension != "jpg" &&
						file_extension != "jpeg" &&
						file_extension != "JPEG" &&
						file_extension != "JPG",
				"The FBX file contains a texture with an unrecognized extension: " + file_extension);

		const String texture_name = absoulte_fbx_file_path.get_file();
		const SpatialMaterial::TextureParam mapping_mode = fbx_mapping_paths.at(fbx_mapping_name);

		Ref<TextureFileMapping> file_mapping;
		file_mapping.instance();
		file_mapping->map_mode = mapping_mode;
		file_mapping->name = texture_name;
		file_mapping->texture = texture.second;
		mappings.push_back(file_mapping);
	}

	return mappings;
}

Ref<SpatialMaterial> FBXMaterial::import_material(ImportState &state) {

	ERR_FAIL_COND_V(material == nullptr, nullptr);

	const String p_fbx_current_directory = state.path;

	Ref<SpatialMaterial> spatial_material;
	spatial_material.instance();

	// read the material file
	// is material two sided
	// read material name
	print_verbose("[material] material name: " + ImportUtils::FBXNodeToName(material->Name()));
	material_name = ImportUtils::FBXNodeToName(material->Name());

	// allocate texture mappings
	const Vector<Ref<TextureFileMapping> > texture_mappings = extract_texture_mappings(material);

	for (int x = 0; x < texture_mappings.size(); x++) {
		Ref<TextureFileMapping> mapping = texture_mappings.get(x);
		Ref<Texture> texture;
		print_verbose("texture mapping name: " + mapping->name);

		if (state.cached_image_searches.has(mapping->name)) {
			texture = state.cached_image_searches[mapping->name];
		} else {
			String path = find_texture_path_by_filename(mapping->name, p_fbx_current_directory);
			if (!path.empty()) {
				Ref<Image> image;
				image.instance();
				Ref<ImageTexture> image_texture;

				if (ImageLoader::load_image(path, image) == OK) {
					image_texture.instance();
					image_texture->create_from_image(image);
					int32_t flags = Texture::FLAGS_DEFAULT;
					image_texture->set_flags(flags);

					texture = image_texture;
					state.cached_image_searches[mapping->name] = texture;
					print_verbose("Created texture from loaded image file.");
				} else {
					ERR_CONTINUE_MSG(true, "unable to import image file not loaded yet TODO");
				}
			} else if (mapping->texture != nullptr && mapping->texture->Media() != nullptr && mapping->texture->Media()->ContentLength() > 0) {
				// This is an embedded texture. Extract it.

				Ref<Image> image;
				image.instance();

				if (
						mapping->name.get_extension() == "png" ||
						mapping->name.get_extension() == "PNG") {

					// The stored file is a PNG.

					const Error err = PNGDriverCommon::png_to_image(
							mapping->texture->Media()->Content(),
							mapping->texture->Media()->ContentLength(),
							image);
					ERR_CONTINUE_MSG(err != OK, "FBX Embedded png image load fail.");

				} else if (
						mapping->name.get_extension() == "jpg" ||
						mapping->name.get_extension() == "jpeg" ||
						mapping->name.get_extension() == "JPEG" ||
						mapping->name.get_extension() == "JPG") {

					// The stored file is a JPEG.

					const Error err = ImageLoaderJPG::jpeg_load_image_from_buffer(
							image.ptr(),
							mapping->texture->Media()->Content(),
							mapping->texture->Media()->ContentLength());
					ERR_CONTINUE_MSG(err != OK, "FBX Embedded jpeg image load fail.");

				} else {
					ERR_CONTINUE_MSG(true, "The embedded image with extension: " + mapping->name.get_extension() + " is not yet supported. Open an issue please.");
				}

				Ref<ImageTexture> image_texture;
				image_texture.instance();
				image_texture->create_from_image(image);

				const int32_t flags = Texture::FLAGS_DEFAULT;
				image_texture->set_flags(flags);

				texture = image_texture;
				state.cached_image_searches[mapping->name] = texture;
				print_verbose("Created texture from embedded image.");
			} else {
				ERR_CONTINUE_MSG(true, "The FBX texture, with name: `" + mapping->name + "`, is not stored as embedded file nor as external file. Make sure to insert the texture as embedded file or into the project, then reimport.");
			}
		}
		spatial_material->set_texture(mapping->map_mode, texture);
	}

	// TODO read other data like colors, UV, etc.. ?

	spatial_material->set_name(material_name);

	return spatial_material;
}