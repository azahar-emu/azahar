void main() {
    gl_Position = vec4(vert_position, 0.0, 1.0);
    frag_tex_coord = vert_tex_coord;
    SMAAEdgeDetectionVS(vert_tex_coord, offset);
}