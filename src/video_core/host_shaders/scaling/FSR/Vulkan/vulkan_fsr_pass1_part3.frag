void main() {
    FsrRcasCon(con0, FSR_SHARPENING);

    AU2 gxy = AU2(frag_tex_coord.xy * o_resolution.xy); // Integer pixel position in output.
    AF3 Gamma2Color = AF3(0, 0, 0);
    FsrRcasF(Gamma2Color.r, Gamma2Color.g, Gamma2Color.b, gxy, con0);

    color = vec4(Gamma2Color, 1.0);
}
