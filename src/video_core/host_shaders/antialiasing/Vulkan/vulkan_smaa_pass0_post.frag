void main() {
    if (SMAA_EDT == 0.0) {
        color = vec4(SMAALumaEdgeDetectionPS(frag_tex_coord, offset, screen_textures[0]), 0.0, 0.0);
    } else if (SMAA_EDT <= 1.0) {
        color = vec4(SMAAColorEdgeDetectionPS(frag_tex_coord, offset, screen_textures[0]), 0.0, 0.0);
    }
}
