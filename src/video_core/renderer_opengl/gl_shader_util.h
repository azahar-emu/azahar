// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <span>
#include <glad/glad.h>

namespace OpenGL {

/**
 * Utility function to create and compile an OpenGL GLSL shader
 * @param source String of the GLSL shader program
 * @param type Type of the shader (GL_VERTEX_SHADER, GL_GEOMETRY_SHADER or GL_FRAGMENT_SHADER)
 * @param debug_name debug name to show in logs
 */
GLuint LoadShader(std::string_view source, GLenum type, const std::string& debug_name);

/**
 * Utility function to create and link an OpenGL GLSL shader program
 * @param separable_program whether to create a separable program
 * @param shaders ID of shaders to attach to the program
 * @param debug_name debug name to show in logs
 * @returns Handle of the newly created OpenGL program object
 */
GLuint LoadProgram(bool separable_program, std::span<const GLuint> shaders,
                   const std::string& debug_name);

} // namespace OpenGL
