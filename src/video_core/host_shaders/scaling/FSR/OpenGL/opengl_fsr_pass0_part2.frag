void main() {
    // --- Setup constants (same as original) ---
    AU4 con0, con1, con2, con3;
    FsrEasuCon(con0, con1, con2, con3,
        i_resolution.x, i_resolution.y,
        i_resolution.x, i_resolution.y,
        o_resolution.x, o_resolution.y);

    AU2 gxy = AU2(frag_tex_coord.xy * o_resolution.xy);

    // --- Get position of 'f' (the center texel of the kernel) ---
    AF2 pp = AF2(gxy) * AF2_AU2(con0.xy) + AF2_AU2(con0.zw);
    AF2 fp = floor(pp);
    pp -= fp;

    // --- Fetch all 12 unique texels directly as RGB ---
    // The 12-tap kernel layout relative to 'fp':
    //    b c          (0,-1) (1,-1)
    //  e f g h   (-1,0) (0,0) (1,0) (2,0)
    //  i j k l   (-1,1) (0,1) (1,1) (2,1)
    //    n o          (0, 2) (1, 2)
    ivec2 sp = ivec2(fp);
    AF3 b = texelFetch(color_texture, sp + ivec2( 0,-1), 0).rgb;
    AF3 c = texelFetch(color_texture, sp + ivec2( 1,-1), 0).rgb;
    AF3 e = texelFetch(color_texture, sp + ivec2(-1, 0), 0).rgb;
    AF3 f = texelFetch(color_texture, sp + ivec2( 0, 0), 0).rgb;
    AF3 g = texelFetch(color_texture, sp + ivec2( 1, 0), 0).rgb;
    AF3 h = texelFetch(color_texture, sp + ivec2( 2, 0), 0).rgb;
    AF3 i = texelFetch(color_texture, sp + ivec2(-1, 1), 0).rgb;
    AF3 j = texelFetch(color_texture, sp + ivec2( 0, 1), 0).rgb;
    AF3 k = texelFetch(color_texture, sp + ivec2( 1, 1), 0).rgb;
    AF3 l = texelFetch(color_texture, sp + ivec2( 2, 1), 0).rgb;
    AF3 n = texelFetch(color_texture, sp + ivec2( 0, 2), 0).rgb;
    AF3 o = texelFetch(color_texture, sp + ivec2( 1, 2), 0).rgb;

    // --- Approximate luma (luma times 2, in 2 FMA/MAD) ---
    AF1 bL = b.b * AF1_(0.5) + (b.r * AF1_(0.5) + b.g);
    AF1 cL = c.b * AF1_(0.5) + (c.r * AF1_(0.5) + c.g);
    AF1 eL = e.b * AF1_(0.5) + (e.r * AF1_(0.5) + e.g);
    AF1 fL = f.b * AF1_(0.5) + (f.r * AF1_(0.5) + f.g);
    AF1 gL = g.b * AF1_(0.5) + (g.r * AF1_(0.5) + g.g);
    AF1 hL = h.b * AF1_(0.5) + (h.r * AF1_(0.5) + h.g);
    AF1 iL = i.b * AF1_(0.5) + (i.r * AF1_(0.5) + i.g);
    AF1 jL = j.b * AF1_(0.5) + (j.r * AF1_(0.5) + j.g);
    AF1 kL = k.b * AF1_(0.5) + (k.r * AF1_(0.5) + k.g);
    AF1 lL = l.b * AF1_(0.5) + (l.r * AF1_(0.5) + l.g);
    AF1 nL = n.b * AF1_(0.5) + (n.r * AF1_(0.5) + n.g);
    AF1 oL = o.b * AF1_(0.5) + (o.r * AF1_(0.5) + o.g);

    // --- Accumulate direction and length ---
    // Inlined FsrEasuSetF for each of the 4 bilinear quadrants.
    // Each quadrant computes gradient direction and edge length
    // from its 5-tap cross pattern centered on the quadrant's
    // nearest texel.
    //
    // Quadrant layout (bilinear weights):
    //  s=(1-x)(1-y)  t=x(1-y)
    //  u=(1-x)y      v=xy
    //
    // Cross pattern for each quadrant:
    //  s: center=f, left=e,  right=g, up=b, down=j
    //  t: center=g, left=f,  right=h, up=c, down=k
    //  u: center=j, left=i,  right=k, up=f, down=n
    //  v: center=k, left=j,  right=l, up=g, down=o

    AF2 dir = AF2_(0.0);
    AF1 len = AF1_(0.0);

    // Quadrant s
    {
        AF1 w = (AF1_(1.0) - pp.x) * (AF1_(1.0) - pp.y);
        AF1 dc = gL - fL; AF1 cb = fL - eL;
        AF1 lenX = max(abs(dc), abs(cb));
        lenX = APrxLoRcpF1(lenX);
        AF1 dirX = gL - eL;
        dir.x += dirX * w;
        lenX = ASatF1(abs(dirX) * lenX); lenX *= lenX; len += lenX * w;
        AF1 ec = jL - fL; AF1 ca = fL - bL;
        AF1 lenY = max(abs(ec), abs(ca));
        lenY = APrxLoRcpF1(lenY);
        AF1 dirY = jL - bL;
        dir.y += dirY * w;
        lenY = ASatF1(abs(dirY) * lenY); lenY *= lenY; len += lenY * w;
    }
    // Quadrant t
    {
        AF1 w = pp.x * (AF1_(1.0) - pp.y);
        AF1 dc = hL - gL; AF1 cb = gL - fL;
        AF1 lenX = max(abs(dc), abs(cb));
        lenX = APrxLoRcpF1(lenX);
        AF1 dirX = hL - fL;
        dir.x += dirX * w;
        lenX = ASatF1(abs(dirX) * lenX); lenX *= lenX; len += lenX * w;
        AF1 ec = kL - gL; AF1 ca = gL - cL;
        AF1 lenY = max(abs(ec), abs(ca));
        lenY = APrxLoRcpF1(lenY);
        AF1 dirY = kL - cL;
        dir.y += dirY * w;
        lenY = ASatF1(abs(dirY) * lenY); lenY *= lenY; len += lenY * w;
    }
    // Quadrant u
    {
        AF1 w = (AF1_(1.0) - pp.x) * pp.y;
        AF1 dc = kL - jL; AF1 cb = jL - iL;
        AF1 lenX = max(abs(dc), abs(cb));
        lenX = APrxLoRcpF1(lenX);
        AF1 dirX = kL - iL;
        dir.x += dirX * w;
        lenX = ASatF1(abs(dirX) * lenX); lenX *= lenX; len += lenX * w;
        AF1 ec = nL - jL; AF1 ca = jL - fL;
        AF1 lenY = max(abs(ec), abs(ca));
        lenY = APrxLoRcpF1(lenY);
        AF1 dirY = nL - fL;
        dir.y += dirY * w;
        lenY = ASatF1(abs(dirY) * lenY); lenY *= lenY; len += lenY * w;
    }
    // Quadrant v
    {
        AF1 w = pp.x * pp.y;
        AF1 dc = lL - kL; AF1 cb = kL - jL;
        AF1 lenX = max(abs(dc), abs(cb));
        lenX = APrxLoRcpF1(lenX);
        AF1 dirX = lL - jL;
        dir.x += dirX * w;
        lenX = ASatF1(abs(dirX) * lenX); lenX *= lenX; len += lenX * w;
        AF1 ec = oL - kL; AF1 ca = kL - gL;
        AF1 lenY = max(abs(ec), abs(ca));
        lenY = APrxLoRcpF1(lenY);
        AF1 dirY = oL - gL;
        dir.y += dirY * w;
        lenY = ASatF1(abs(dirY) * lenY); lenY *= lenY; len += lenY * w;
    }

    // --- Normalize direction ---
    AF2 dir2 = dir * dir;
    AF1 dirR = dir2.x + dir2.y;
    AP1 zro = dirR < AF1_(1.0 / 32768.0);
    dirR = APrxLoRsqF1(dirR);
    dirR = zro ? AF1_(1.0) : dirR;
    dir.x = zro ? AF1_(1.0) : dir.x;
    dir *= AF2_(dirR);

    // --- Shape length ---
    len = len * AF1_(0.5);
    len *= len;
    AF1 stretch = (dir.x * dir.x + dir.y * dir.y) * APrxLoRcpF1(max(abs(dir.x), abs(dir.y)));
    AF2 len2 = AF2(AF1_(1.0) + (stretch - AF1_(1.0)) * len, AF1_(1.0) + AF1_(-0.5) * len);
    AF1 lob = AF1_(0.5) + AF1_((1.0 / 4.0 - 0.04) - 0.5) * len;
    AF1 clp = APrxLoRcpF1(lob);

    // --- Min/max of 4 nearest (f, g, j, k) for de-ringing ---
    AF3 min4 = min(min(f, g), min(j, k));
    AF3 max4 = max(max(f, g), max(j, k));

    // --- Accumulate 12 taps (inlined FsrEasuTapF) ---
    AF3 aC = AF3_(0.0);
    AF1 aW = AF1_(0.0);

    // Macro for the Lanczos-like kernel evaluation per tap.
    // Rotates offset by direction, applies anisotropic scaling,
    // evaluates the approximated windowed Lanczos kernel, accumulates.
    #define FSR_EASU_TAP(OFF_X, OFF_Y, COLOR) { \
        AF2 v; \
        v.x = ((OFF_X) - pp.x) * dir.x + ((OFF_Y) - pp.y) * dir.y; \
        v.y = ((OFF_X) - pp.x) * (-dir.y) + ((OFF_Y) - pp.y) * dir.x; \
        v *= len2; \
        AF1 d2 = min(v.x * v.x + v.y * v.y, clp); \
        AF1 wB = AF1_(2.0 / 5.0) * d2 + AF1_(-1.0); \
        AF1 wA = lob * d2 + AF1_(-1.0); \
        wB *= wB; wA *= wA; \
        wB = AF1_(25.0 / 16.0) * wB + AF1_(-(25.0 / 16.0 - 1.0)); \
        AF1 w = wB * wA; \
        aC += (COLOR) * w; aW += w; }

    FSR_EASU_TAP( 0.0, -1.0, b)  // b
    FSR_EASU_TAP( 1.0, -1.0, c)  // c
    FSR_EASU_TAP(-1.0,  1.0, i)  // i
    FSR_EASU_TAP( 0.0,  1.0, j)  // j
    FSR_EASU_TAP( 0.0,  0.0, f)  // f
    FSR_EASU_TAP(-1.0,  0.0, e)  // e
    FSR_EASU_TAP( 1.0,  1.0, k)  // k
    FSR_EASU_TAP( 2.0,  1.0, l)  // l
    FSR_EASU_TAP( 2.0,  0.0, h)  // h
    FSR_EASU_TAP( 1.0,  0.0, g)  // g
    FSR_EASU_TAP( 1.0,  2.0, o)  // o
    FSR_EASU_TAP( 0.0,  2.0, n)  // n

    #undef FSR_EASU_TAP

    // --- Normalize and de-ring ---
    AF3 pix = min(max4, max(min4, aC * AF3_(ARcpF1(aW))));
    color = vec4(pix, 1.0);
}
