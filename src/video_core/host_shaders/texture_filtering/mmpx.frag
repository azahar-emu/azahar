// Copyright 2023 Citra Emulator Project
// 2025 Enhanced by CrashGG.
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

//? #version 430 core
precision highp float;

layout(location = 0) in vec2 tex_coord;
layout(location = 0) out vec4 frag_color;
layout(binding = 0) uniform sampler2D tex;
///////////////////////////////////////////

// RGB perceptual weight + alpha segmentation
float luma(vec4 col) {

    // Use BT.601 standard from CRT era, divide result by 10 to get range [0.0 - 0.1]
    float rgbsum = dot(col.rgb, vec3(0.0299, 0.0587, 0.0114));

    // Take decimal part then *10 to remove alpha weighting in subsequent steps
    float alphafactor = 
        (col.a > 0.854102) ? 1.0 :        // 2nd short golden ratio (larger part)
        (col.a > 0.618034) ? 2.0 :        // 1st golden ratio
        (col.a > 0.381966) ? 3.0 :        // 1st short golden ratio
        (col.a > 0.145898) ? 4.0 :        // 2nd short golden ratio
        (col.a > 0.002) ? 5.0 : 8.0;      // Fully transparent

    return rgbsum + alphafactor;

}

/* Constant explanations:
0.145898    :   2nd short golden ratio of 1.0
0.0638587   :   Squared 2nd short golden ratio of RGB Euclidean distance
0.024391856 :   Squared (2nd short + 1st) golden ratio of RGB Euclidean distance
0.00931686  :   Squared 3rd short golden ratio of RGB Euclidean distance
0.001359312 :   Squared 4th short golden ratio of RGB Euclidean distance
0.4377      :   Squared 1st short golden ratio of RGB Euclidean distance
0.75        :   Squared half of RGB Euclidean distance
*/
// Pixel similarity detection LV1
bool sim1(vec4 col1, vec4 col2) {

    vec4 diff = col1 - col2;
    vec4 absdiff = abs(diff);

    // 1. Fast component difference check
    if ( absdiff.r > 0.1 || absdiff.g > 0.1 || absdiff.b > 0.1 || absdiff.a > 0.145898 ) return false;   // xxx.alpha

    // 2. Fast squared distance check
    float dot_diff = dot(diff.rgb, diff.rgb);       // xxx.alpha
    if (dot_diff < 0.001359312) return true;

    // 3. Gradual pixel check
    float min_diff = min(diff.r, min(diff.g, diff.b));
    float max_diff = max(diff.r, max(diff.g, diff.b));
    if ( max_diff-min_diff>0.096 ) return false;    // Exit if difference exceeds int24 range
    if ( max_diff-min_diff<0.024 && dot_diff<0.024391856)  return true;  // Consider gradual pixel if difference ≤ int6, relax threshold by one level

    // 4. Grayscale pixel check
    float sum1 = dot(col1.rgb, vec3(1.0));  // Sum of RGB channels  //xxx.alpha
    float sum2 = dot(col2.rgb, vec3(1.0));                      //xxx.alpha
    float avg1 = sum1 * 0.3333333;
    float avg2 = sum2 * 0.3333333;

    vec3 graydiff1 = col1.rgb - vec3(avg1); //xxx.alpha
    vec3 graydiff2 = col2.rgb - vec3(avg2); //xxx.alpha
    float dotgray1 = dot(graydiff1,graydiff1);
    float dotgray2 = dot(graydiff2,graydiff2);
    // 0.002: Allow max single-channel difference of int13 when avg=20
    // 0.0004: Allow max single-channel difference of int6, or int3+4 across two channels
    float tolerance1 = avg1<0.08 ? 0.002 : 0.0004;
    float tolerance2 = avg2<0.08 ? 0.002 : 0.0004;
    // 0.078: Limit max green channel value to 19 (human eye perception threshold)
    bool Col1isGray = sum1<0.078||dotgray1<tolerance1;
    bool Col2isGray = sum2<0.078||dotgray2<tolerance2;

    // Relax standard to Lv2 if both are grayscale
    if ( Col1isGray && Col2isGray && dot_diff<0.024391856 ) return true;

    // Exit if only one is grayscale
    if ( Col1isGray != Col2isGray ) return false;

    // Accumulate positive/negative differences separately using max/min
    float team_pos = abs(dot(max(diff.rgb, 0.0), vec3(1.0))); //xxx.alpha
    float team_neg = abs(dot(min(diff.rgb, 0.0), vec3(1.0))); //xxx.alpha
    // Find opposing channel values and add to squared distance for final check
    float team_rebel = min(team_pos, team_neg);
    // Implementation: Need to add at least 3x opposing channel values (1x to break even, 2x to create increasing trend)
    return dot_diff + team_rebel*team_rebel*3.0 < 0.00931686;

}

// Pixel similarity detection LV2 and LV3
bool sim2n3(vec4 col1, vec4 col2, int Lv) {

    // Ignore RGB if both points are nearly transparent  //xxx.alpha
    if ( col1.a < 0.381966 && col2.a < 0.381966 ) return true;
    // Alpha channel difference cannot exceed 0.382  //xxx.alpha
    if ( abs(col1.a-col2.a)>0.381966) return false;

    // 1. Clamp RGB dark areas
    vec3 clampCol1 = max(col1.rgb, vec3(0.078)); //xxx.alpha
    vec3 clampCol2 = max(col2.rgb, vec3(0.078)); //xxx.alpha

    vec3 clampdiff = clampCol1 - clampCol2;

    // RGB Euclidean distance threshold: (2 short + 1) golden ratio for Lv2, 2nd short golden ratio for Lv3
    float dotdist = Lv==2 ? 0.024391856 : 0.0638587;

    return dot(clampdiff, clampdiff) < dotdist;
}

bool sim2(vec4 colC1, uint C1, uint C2) {
    if (C1==C2) return true;
    return sim2n3(colC1, unpackUnorm4x8(C2), 2);
}

bool sim3(vec4 col1, vec4 col2) {
    return sim2n3(col1, col2, 3);
}

bool mixcheck(vec4 col1, vec4 col2) {

    // Disallow mixing if only one is transparent        //xxx.alpha
    bool col1alpha0 = col1.a < 0.003;
    bool col2alpha0 = col2.a < 0.003;
    if (col1alpha0!=col2alpha0) return false;

    // Disallow mixing if alpha difference exceeds 50%  //xxx.alpha
    if (abs(col1.a - col2.a)>0.5) return false;

    vec3 diff = col1.rgb - col2.rgb;

    // Gradual pixel check
    float min_diff = min(diff.r, min(diff.g, diff.b));
    float max_diff = max(diff.r, max(diff.g, diff.b));
    if ( max_diff-min_diff>0.618034 ) return false;

    float dot_diff = dot(diff, diff);
    if( max_diff-min_diff<0.024 && dot_diff<0.75)  return true;  //  0.020 ≤ int5, 0.024 ≤ int6

    // Accumulate positive/negative differences separately using max/min
    float team_pos = abs(dot(max(diff, 0.0), vec3(1.0)));
    float team_neg = abs(dot(min(diff, 0.0), vec3(1.0)));
    // Find opposing channel values and add to squared distance for final check
    float team_rebel = min(team_pos, team_neg);
    // Implementation: Need to add at least 3x opposing channel values (1x to break even, 2x to create increasing trend)
    return dot_diff + team_rebel*team_rebel*3.0 < 0.4377;
}

// RGB must match exactly, alpha channel allows minor differences
bool eq(uint C1, uint C2){
    if (C1 == C2) return true;

    uint rgbC1 = C1 & 0x00FFFFFFu;
    uint rgbC2 = C2 & 0x00FFFFFFu;

    if (rgbC1 != rgbC2) return false;

    uint alphaC1 = C1 >> 24;
    uint alphaC2 = C2 >> 24;

    // Note: abs(alphaC1-alphaC2) cannot be used with uint!
    uint alphaDiff = (alphaC1 > alphaC2) ? (alphaC1 - alphaC2) : (alphaC2 - alphaC1);

    return alphaDiff < 38u; // 2 short golden ratio of 255u

}

#define noteq(a,b) (a!=b)

bool vec_noteq(vec4 col1, vec4 col2) {
    vec4 diff = abs(col1 - col2);
    // Allow total RGB channel difference of int2, alpha channel difference of int5
    return dot(diff.rgb, vec3(1.0)) > 0.008 || diff.a > 0.021286;
}

bool all_eq2(uint B, uint A0, uint A1) {
    return (eq(B,A0) && eq(B,A1));
}

bool any_eq2(uint B, uint A0, uint A1) {
   return (eq(B,A0) || eq(B,A1));
}

bool any_eq3(uint B, uint A0, uint A1, uint A2) {
   return (eq(B,A0) || eq(B,A1) || eq(B,A2));
}

bool none_eq2(uint B, uint A0, uint A1) {
   return (noteq(B,A0) && noteq(B,A1));
}

// Pre-define
//#define testcolor vec4(1.0, 0.0, 1.0, 1.0)  // Magenta
//#define testcolor2 vec4(0.0, 1.0, 1.0, 1.0)  // Cyan
//#define testcolor3 vec4(1.0, 1.0, 0.0, 1.0)  // Yellow
//#define testcolor4 vec4(1.0, 1.0, 1.0, 1.0)  // White

#define slopeBAD vec4(2.0)
#define theEXIT vec4(4.0)
#define slopOFF vec4(8.0)
#define Mix382 mix(colX,colE,0.381966)
#define Mix618 mix(colX,colE,0.618034)
#define Mix854 mix(colX,colE,0.8541)
#define Mix382off Mix382+slopOFF
#define Mix618off Mix618+slopOFF
#define Mix854off Mix854+slopOFF
#define Xoff colX+slopOFF
#define checkblack(col) ((col).g < 0.078 && (col).r < 0.1 && (col).b < 0.1)
#define diffEB abs(El-Bl)
#define diffED abs(El-Dl)

//pin zz
// Concave + Cross shape - weak mixing (weak/none)
vec4 admixC(vec4 colX, vec4 colE) {
    // Transparent pixels already filtered in main pipeline

    bool mixok = mixcheck(colX, colE);

    return mixok ? Mix618 : colE;

}

// K shape - forced weak mixing (weak/weaker)
vec4 admixK(vec4 colX, vec4 colE) {
    // Transparent pixels already filtered in main pipeline

    bool mixok = mixcheck(colX, colE);

    return mixok ? Mix618 : Mix854;

}

// L shape - 2:1 slope (main corner extension)
// Implementation: This rule requires 4 pixels on slope to be identical to avoid artifacts!
vec4 admixL(vec4 colX, vec4 colE, vec4 colS) {

    // eqX,E check originally catches many duplicate pixels, now filtered by slopeok in main pipeline

    // Return colX directly if target X differs from sample S (already mixed once)
    if (vec_noteq(colX, colS)) return colX;

    bool mixok = mixcheck(colX, colE);

    return mixok ? Mix382 : colX;
}

/********************************************************************************************************************************************
 *              												main slope + X cross-processing mechanism					                *
 *******************************************************************************************************************************************/
vec4 admixX(uint A, uint B, uint C, uint D, uint E, uint F, uint G, uint H, uint I, uint P, uint PA, uint PC, uint Q, uint QA, uint QG, uint R, uint RC, uint RI, uint S, uint SG, uint SI, uint AA, uint CC, uint GG, float El, float Bl, float Dl, float Fl, float Hl, vec4 colE, vec4 colB, vec4 colD) {

    // xxx.alpha
    // 1. Illogical for B/D to be transparent when center E has higher luma (normal rule)
    // 2. B/D might be transparent when E-A match, but using transparent B/D to cut solid E-A cross is against design
    if (colB.a<0.002||colD.a<0.002) return slopeBAD;

    bool eq_B_C = eq(B, C);
    bool eq_D_G = eq(D, G);

    // Exit if surrounded by straight walls on both sides
    if (eq_B_C && eq_D_G) return slopeBAD;

    //Pre-declare
    bool eq_A_B;        bool eq_A_D;        bool eq_A_P;    bool eq_A_Q;
    bool eq_B_P;        bool eq_B_PA;    bool eq_B_PC;
    bool eq_D_Q;        bool eq_D_QA;    bool eq_D_QG;
    bool eq_E_F;        bool eq_E_H;        bool eq_E_I;    bool En3;
    bool B_slope;    bool B_tower;    bool B_wall;
    bool D_slope;    bool D_tower;    bool D_wall;
    vec4 colX;        bool Xisblack;
    bool mixok;

    bool eq_B_D = eq(B,D);
    bool eq_E_A = eq(E,A);
    
    #define comboA3  eq_A_P && eq_A_Q
    #define En4square  En3 && eq_E_I

    // E is nearly transparent but not fully transparent (mostly edges)  //xxx.alpha
    bool EalphaL = colE.a <0.381966 && colE.a >0.002;

    // Remove alpha weighting
    Bl = fract(Bl) *10.0;
    Dl = fract(Dl) *10.0;
    El = fract(El) *10.0;
    Fl = fract(Fl) *10.0;
    Hl = fract(Hl) *10.0;

/*=========================================
                    B != D
  ==================================== zz */
if (!eq_B_D){

    // Exit if E-A match (violates preset logic)
    if (eq_E_A) return slopeBAD;

    // Exit if B-D difference is larger than E-B or E-D difference
    float diffBD = abs(Bl-Dl);
    if (diffBD > diffEB || diffBD > diffED) return slopeBAD;

    // Prevent single-pixel font edges from being squeezed by black background (luma difference usually >0.5)
    // Note: If B≠D, must check both for black condition (cannot use average)
    Xisblack = checkblack(colB) && checkblack(colD);
    if ( Xisblack && El >0.5 && (Fl<0.078 || Hl<0.078) ) return theEXIT;

// Pre-filter rules before original logic (triangle vertices cannot be convex)
    eq_A_B = eq(A,B);
    if ( !Xisblack && eq_A_B && eq_D_G && eq(B,P) ) return slopeBAD;

    eq_A_D = eq(A,D);
    if ( !Xisblack && eq_A_D && eq_B_C && eq(D,Q) ) return slopeBAD;

    // Should B/D have no relation to surrounding pixels? Removing this fixes some artifacts but loses shapes (e.g. Double Dragon attract mode sprites)

    // Mix B/D to get X
    colX = mix(colB, colD, 0.5);
    colX.a = min(colB.a, colD.a);   // xxx.alpha

    mixok = mixcheck(colX,colE);

    eq_A_P = eq(A,P);
    eq_A_Q = eq(A,Q);
    // High priority for A-side triple consecutive match (eq_A_P && eq_A_Q)
    if (comboA3) return mixok ? Mix382off : Xoff;

    // Official original rule
    if ( eq(E,C) || eq(E,G) ) return mixok ? Mix382off : Xoff;

    // Enhanced original rule 1 (good for trend detection, but rule 2 breaks wall logic)
    if ( !eq_D_G&&eq(E,QG)&&sim2(colE,E,G) || !eq_B_C&&eq(E,PC)&&sim2(colE,E,C) ) return mixok ? Mix382off : Xoff;

    // Exclude "3-pixel single-side wall" cases
    if (!Xisblack){
        if ( eq_A_B&&eq_B_C || eq_A_D&&eq_D_G ) return slopeBAD;
    }

    //xxx.alpha
    if (EalphaL) return mixok ? Mix382off : Xoff;

    // F-H inline trend (includes En3, placed after wall rules)
    if ( eq(F,H) ) return mixok ? Mix382off : Xoff;

    // Abandon remaining "2-pixel single-side wall" and orphan pixels
    return slopeBAD;
} // B != D

/*******  B == D prepare *******/
  
    // Prevent single-pixel font edges from being squeezed by black background
    Xisblack = checkblack(colB);
    if ( Xisblack && El >0.5 && (Fl<0.078 || Hl<0.078) ) return theEXIT;

    colX = colB;
    colX.a = min(colB.a , colD.a);

    bool eq_E_C = eq(E,C);
    bool eq_E_G = eq(E,G);
    bool sim_EC = eq_E_C || sim2n3(colE,unpackUnorm4x8(C),2);
    bool sim_EG = eq_E_G || sim2n3(colE,unpackUnorm4x8(G),2);

    bool ThickBorder;


/*===============================================
                 Original main rule with sim2 enhancement
  ========================================== zz */
if ( (sim_EC || sim_EG) && !eq_E_A ){

/* Logic flow:
    1. Handle continuous boundary shapes (no mixing)
    2. Special handling for long slopes
    3. Original rules
    4. Handle E-area inline patterns (En4, En3, F-H), remaining single bars/pixels
    5. Handle L-shaped inner/outer single bars
    6. Default fallback return
*/

    eq_A_B = eq(B,A);
    eq_B_P = eq(B,P);
    eq_B_PC = eq(B,PC);
    eq_B_PA = eq(B,PA);
    eq_D_Q = eq(D,Q);
    eq_D_QG = eq(D,QG);
    eq_D_QA = eq(D,QA);
    B_slope = eq_B_PC && !eq_B_P && !eq_B_C;
    B_tower = eq_B_P && !eq_B_PC && !eq_B_C && !eq_B_PA;
    D_slope = eq_D_QG && !eq_D_Q && !eq_D_G;
    D_tower = eq_D_Q && !eq_D_QG && !eq_D_G && !eq_D_QA;

    // Return Xoff (no mixing) for strong continuous B+/D+ shapes
    if ( (B_slope||B_tower) && (D_slope||D_tower) && !eq_A_B) return Xoff;

    eq_A_P = eq(A, P);
    eq_A_Q = eq(A, Q);
    mixok = mixcheck(colX,colE);
    ThickBorder = eq_A_B && (eq_A_P||eq_A_Q|| eq(A,AA)&&(eq_B_PA||eq_D_QA));
    if (ThickBorder && !Xisblack) mixok=false;

    // A-side triple consecutive match (eq_A_P && eq_A_Q)
    if (comboA3) {
        if (!eq_A_B) return Xoff;
        else mixok=false;
    }

    // XE_messL B-D-E L-shape with high sim2 similarity    WIP 
    // bool XE_messL = (eq_B_C && !sim_EG || eq_D_G && !sim_EC) ;

    eq_E_F = eq(E, F);
    B_wall = eq_B_C && !eq_B_PC && !eq_B_P;

    // Long clear slope (not thick solid edge - strong trend!)
    // Special case handling for long slopes
    if ( B_wall && D_tower ) {
        if (eq_E_G || sim_EG&&eq(E,QG) ) {   // Original rule + enhanced
            if (eq_A_B) return mixok ? Mix382 : colX;    // Has thickness
            return colX;                               // Hollow
        }
        // Clear zig-zag shape - exclude eq_A_B + XE_messL ???
        //if (eq_A_B  && !XE_messL) return ; // Poor results
        if (eq_A_B) return slopeBAD;
        // Double-pixel with extended long slope
        if (eq_E_F ) return colX;
        // Single-pixel without extended long slope
        return Xoff;
    }

    eq_E_H = eq(E, H);
    D_wall = eq_D_G && !eq_D_QG && !eq_D_Q;

    if ( B_tower && D_wall ) {
        if (eq_E_C || sim_EC&&eq(E,PC) ) {   // Original rule + enhanced
            if (eq_A_B) return mixok ? Mix382 : colX;    // Has thickness
            return colX;                                // Hollow
        }
        // Clear zig-zag shape - exclude eq_A_B + XE_messL ???
        //if (eq_A_B  && !XE_messL) return ; // Poor results
        if (eq_A_B) return slopeBAD;
        // Double-pixel with extended long slope
        if (eq_E_H ) return colX;
        // Single-pixel without extended long slope
        return Xoff;
    }

    // Official original rule (placed after special shapes which have explicit no-mix rules)
    if (eq_E_C || eq_E_G) return mixok ? Mix382 : colX;

    // Enhanced original rule 1
    if (sim_EG&&!eq_D_G&&eq(E,QG) || sim_EC&&!eq_B_C&&eq(E,PC)) return mixok ? Mix382off : Xoff;

    // Enhanced original rule 2
    if (sim_EC && sim_EG) return mixok ? Mix382off : Xoff;

    // F-H inline trend (skip En4/En3)
    if ( eq(F,H) )  return mixok ? Mix382off : Xoff;

    // Final cleanup for two long slopes (non-clear shapes) using relaxed rules
    // Implementation: This section handles different cases than F-H (neutral by default unless cube shape)
    if (eq_B_C && eq_D_Q) {
        // Exit for double cube shape
        if (eq_B_P && eq_B_PC && eq_A_B && eq_D_QA && !eq_D_QG && eq_E_F && eq(H,I) ) return theEXIT;

        return mixok ? Mix382off : Xoff;
    }

    if ( eq_D_G && eq_B_P) {
        // Exit for double cube shape
        if (eq_D_Q && eq_D_QG && eq_A_B && eq_B_PA && !eq_B_PC && eq_E_H && eq(F,I) ) return theEXIT;

        return mixok ? Mix382off : Xoff;
    }

    eq_E_I = eq(E, I);

    // Exit for clear L-shaped corner with inline parallel bar
    if (eq_A_B && !ThickBorder && !eq_E_I ) {
        if (B_wall && eq_E_F) return theEXIT;
        if (D_wall && eq_E_H) return theEXIT;
    }

    // Early return if colors are similar
    if (mixok) return Mix382off;
    if (EalphaL) return mixok ? Mix382off : Xoff;   //xxx.alpha

    // Exit for hollow L-shaped corner with outer bar (fixes font edge issues)
    if ( !eq_A_B && (eq_E_F||eq_E_H) && !eq_E_I) {
        if (B_tower && !eq_D_Q && !eq_D_QG) return theEXIT;
        if (D_tower && !eq_B_P && !eq_B_PC) return theEXIT;
    }

    // Fallback handling
    return mixok ? Mix382off : Xoff;

} // sim2 base

/*===================================================
                    E - A cross
  ============================================== zz */
if (eq_E_A) {

    // Cross detection requires "region" and "trend" concepts - tighter conditions for different regions

    // No need to exit for unrelated B/D pixels here!

    eq_E_F = eq(E, F);
    eq_E_H = eq(E, H);
    eq_E_I = eq(E, I);

    En3 = eq_E_F&&eq_E_H;

    // Special shape: square (En3 && eq_E_I)
    if ( En4square ) {
        if( noteq(G,H) && noteq(C,F)                      // Independent clear 4-pixel square / 6-pixel rectangle (both sides match)
        && (eq(H,S) == eq(I,SI) && eq(F,R) == eq(I,RI)) ) return theEXIT;
        //else return mixok ? mix(X,E,0.381966) : X;    Do not return directly - need board scoring for adjacent B/D square bubbles
    }

    // Special shape: dot pattern
    // Implementation 1: Use !eq_E_F/!eq_E_H instead of !sim to preserve shapes
    // Implementation 2: Force mixing to preserve shapes (e.g. Gouketsuji Ichizoku horse, Saturday Night Slam Masters 2)

    bool Eisblack = checkblack(colE);
    mixok = mixcheck(colX,colE);

    // 1. Dot pattern center
    if ( eq_E_C && eq_E_G && eq_E_I && !eq_E_F && !eq_E_H ) {

        // Exit if center E is black (fixes KOF96 power bar, Punisher's belt) to avoid high-contrast mixing
        if (Eisblack) return theEXIT;
        // Implementation 1: Do not check black B pixels (normal logic entry)
        // Implementation 2: No need to check eq_F_H separately (95% hit rate) + fallback mixing for layered gradients
        return mixok ? Mix382off : Mix618off;
    }

    eq_A_P = eq(A,P);
    eq_A_Q = eq(A,Q);
    eq_B_PA = eq(B,PA);

    // 2. Dot pattern edge
    if ( eq_A_P && eq_A_Q && eq(A,AA) && noteq(A,PA) && noteq(A,QA) )  {
        if (Eisblack) return theEXIT;

        // Use strong mixing for layered gradient edges
        if ( !eq_B_PA && eq(PA,QA) ) return mixok ? Mix382off : Mix618off;
        // Weak mixing for perfect cross (dot pattern edge) - fallback weak mixing (no need to handle health bar borders separately)
        return mixok ? Mix618off : Mix854off;
    }

    // xxx.alpha
    if (colE.a<0.002) return colX;

    eq_D_QA = eq(D,QA);
    eq_D_QG = eq(D,QG);
    eq_B_PC = eq(B,PC);

    // 3. Half dot pattern (usually shadow on outline edges) - use weak mixing
    if ( eq_E_C && eq_E_G && eq_A_P && eq_A_Q &&
        (eq_B_PC || eq_D_QG) &&
         eq_D_QA && eq_B_PA) {
        //if (Eisblack) return ; Unnecessary

        return mixok ? Mix618off : Mix854off;
    }

    // 4. Quarter dot pattern (prone to "pinkie finger" artifacts - e.g. SF2 Guile's plane, Cadillacs and Dinosaurs character select)
    if ( eq_E_C && eq_E_G && eq_A_P
         && eq_B_PA &&eq_D_QA && eq_D_QG
         && eq_E_H
        ) return mixok ? Mix618off : Mix854off;

    if ( eq_E_C && eq_E_G && eq_A_Q
         && eq_B_PA &&eq_D_QA && eq_B_PC
         && eq_E_F
        ) return mixok ? Mix618off : Mix854off;

    // E-side triple consecutive match (must come after dot pattern checks)
    if ( eq_E_C && eq_E_G ) return Xoff;

    // A-side triple consecutive match (eq_A_P && eq_A_Q) - prioritize mixing over copy since E-A match
    if (comboA3) return mixok ? Mix382off : Xoff;

    // B-D part of long slope
    eq_B_P = eq(B,P);
    eq_D_Q = eq(D,Q);
    B_wall = eq_B_C && !eq_B_P;
    B_tower = eq_B_P && !eq_B_C;
    D_tower = eq_D_Q && !eq_D_G;
    D_wall = eq_D_G && !eq_D_Q;

    int scoreE = 0; int scoreB = 0; int scoreD = 0; int scoreZ = 0;

// E B D area board scoring rules

//  E Zone
    if (En3) {
        scoreE += 1;
        if (B_wall || B_tower || D_tower || D_wall) scoreZ = 1;
    }

    if (eq_E_C) {
        scoreE += 1;
        scoreE += int(eq_E_F);
    }

    if (eq_E_G) {
        scoreE += 1;
        scoreE += int(eq_E_H);

    }

    if (scoreE==0) {
        // Single bar
        if (eq_E_F ||eq_E_H) return theEXIT;
    }

    if ( eq(F,H) ) {
        scoreE += 1;
        if ( scoreZ==0 && B_wall && (eq(F,R) || eq(G,H) || eq(F,I)) ) scoreZ = 1;
        if ( scoreZ==0 && D_wall && (eq(C,F) || eq(H,S) || eq(F,I)) ) scoreZ = 1;
    }

    #define Bn3  eq_B_P&&eq_B_C
    #define Dn3  eq_D_G&&eq_D_Q

//  B Zone
    scoreB -= int(Bn3);
    scoreB -= int(eq(C,P));
    if (scoreB < 0) scoreZ = 0;

    if (eq_B_PA) {
        scoreB -= 1;
        scoreB -= int(eq_B_P);    // Replace eq(P,PA)
    }

//  D Zone
    scoreD -= int(Dn3);
    scoreD -= int(eq(G,Q));
    if (scoreD < 0) scoreZ = 0;

    if (eq_D_QA) {
        scoreD -= 1;
        scoreD -= int(eq_D_Q);    // Replace eq(Q,QA)
    }

    int scoreFinal = scoreE + scoreB + scoreD + scoreZ ;

    if (scoreE >= 1 && scoreB >= 0 && scoreD >=0) scoreFinal += 1;

    if (scoreFinal >= 2) return colX;

    if (scoreFinal == 1) return mixok ? Mix382 : colX;

    // Final addition: long slope shape with zero total score and no B/D penalties
    if (scoreB >= 0 && scoreD >=0) {
        if (B_wall&&D_tower) return colX;
        if (B_tower&&D_wall) return colX;
    }

    return slopeBAD;

}   // eq_E_A

/*=========================================================
                   F - H / B+ D+ extension new rules
  ==================================================== zz */

// This section differs from sim section - natural wall separation between center/En4square/BD
// Different judgment rules from sim side

    eq_B_P = eq(B, P);
    eq_B_PC = eq(B, PC);
    eq_D_Q = eq(D, Q);
    eq_D_QG = eq(D, QG);

    // Exit if B/D have no relation to surrounding pixels (required for this branch)
    if ( !eq_B_C && !eq_B_P && !eq_B_PC && !eq_D_G && !eq_D_Q && !eq_D_QG ) return slopeBAD;

    mixok = mixcheck(colX,colE);
    eq_E_I = eq(E, I);

    // Exit if center E is single high-contrast pixel
    // Tighten threshold when E is highlight
    float E_lumDiff = El > 0.92 ? 0.145898 : 0.381966;
    // Large difference with surroundings
    if ( !mixok && !eq_E_I && !EalphaL && abs(El-Fl)>E_lumDiff && abs(El-Hl)>E_lumDiff ) return slopeBAD;   //xxx.alpha

    eq_A_B = eq(A, B);
    eq_A_P = eq(A, P);
    eq_A_Q = eq(A, Q);
    eq_B_PA = eq(B,PA);
    eq_D_QA = eq(D,QA);

    ThickBorder = eq_A_B && (eq_A_P||eq_A_Q|| eq(A,AA)&&(eq_B_PA||eq_D_QA));

    if (ThickBorder && !Xisblack) mixok=false;

    B_slope = eq_B_PC && !eq_B_P && !eq_B_C;
    B_tower = eq_B_P && !eq_B_PC && !eq_B_C && !eq_B_PA;
    D_slope = eq_D_QG && !eq_D_Q && !eq_D_G;
    D_tower = eq_D_Q && !eq_D_QG && !eq_D_G && !eq_D_QA;

    if (!eq_A_B) {
        // B + D continuous shapes
        // Implementation 1: Looser conditions for one side if other side has clear shape
        // Implementation 2: Flat outer edge, inner edge can be tower (not wall) for "厂" shape
        if ( (B_slope||B_tower) && (eq_D_QG&&!eq_D_G||D_tower) ) return Xoff;
        if ( (D_slope||D_tower) && (eq_B_PC&&!eq_B_C||B_tower) ) return Xoff;

        // High priority for A-side triple consecutive match (eq_A_P && eq_A_Q)
        if (comboA3) return Xoff;

        // combo 2x2 as supplement
        if ( B_slope && eq_A_P ) return mixok ? Mix382off : Xoff;
        if ( D_slope && eq_A_Q ) return mixok ? Mix382off : Xoff;
    }

    eq_E_F = eq(E, F);
    B_wall = eq_B_C && !eq_B_PC && !eq_B_P;

    // Long clear slope (not solid edge - strong trend!)
    // Special case handling for long slopes
    if ( B_wall && D_tower ) {
        if (eq_A_B ) return slopeBAD;
        if (eq_E_F ) return colX;  //wip: Test direct X return (no mixing)
        return mixok ? Mix382off : Xoff;
    }

    eq_E_H = eq(E, H);
    D_wall = eq_D_G && !eq_D_QG&& !eq_D_Q;

    if ( B_tower && D_wall ) {
        if (eq_A_B ) return slopeBAD;
        if (eq_E_H ) return colX;
        return mixok ? Mix382off : Xoff;
    }

    bool sim_X_E = sim3(colX,colE);
    bool eq_G_H = eq(G, H);
    bool eq_C_F = eq(C, F);
    bool eq_H_S = eq(H, S);
    bool eq_F_R = eq(F, R);

    En3 = eq_E_F&&eq_E_H;

    // 4-pixel rectangle inside wall
    if ( En4square ) {  // This square check must come after previous rules
        if (sim_X_E) return mixok ? Mix382off : Xoff;
        // Exit for solid L-shaped inner wrap (fixes font edge corners, Mega Man 7 building corners)
        if ( (eq_B_C || eq_D_G) && eq_A_B ) return theEXIT;
        //if (eq_H_S && eq_F_R) return theEXIT; // Extended on both sides
        // L-shaped inner wrap (hollow corner) / high-contrast independent 4-pixel square / 6-pixel rectangle (check both edges)
        if ( ( eq_B_C&&!eq_G_H || eq_D_G&&!eq_C_F || !eq_G_H&&!eq_C_F&&diffEB>0.5) && (eq_H_S == eq(I, SI) && eq_F_R == eq(I, RI)) ) return theEXIT;

        return mixok ? Mix382off : Xoff;
    }

    // Triangle inside wall
    if ( En3 ) {
        if (sim_X_E) return mixok ? Mix382off : Xoff;
        if (eq_H_S && eq_F_R) return theEXIT; // Extended on both sides (building edges)
       // Inner curve
        if (eq_B_C || eq_D_G) return mixok ? Mix382off : Xoff;
        // Direct return for thick shapes (Zig-zag can flatten outer curve below)
        if (eq_A_B) return mixok ? Mix382off : Xoff;
        // Outer curve
        if (eq_B_P || eq_D_Q) return theEXIT;

        return mixok ? Mix382off : Xoff;
        // Final two rules based on experience: connect inner L-curves, not outer (Double Dragon Jimmy's eyebrows)
    }

    // F - H
    // Principle: connect inner L-curves, not outer
    if ( eq(F,H) ) {
        if (sim_X_E||EalphaL) return mixok ? Mix382off : Xoff;   //xxx.alpha
        // Solid L-shaped inner wrap - avoid squeezing single pixel with symmetric wrap
        if ( eq_B_C && eq_A_B && (eq_G_H||!eq_F_R) &&eq(F, I) ) return slopeBAD;
        if ( eq_D_G && eq_A_B && (eq_C_F||!eq_H_S) &&eq(F, I) ) return slopeBAD;

        // Inner curve
        if (eq_B_C && (eq_F_R||eq_G_H||eq(F, I))) return mixok ? Mix382off : Xoff;
        if (eq_D_G && (eq_C_F||eq_H_S||eq(F, I))) return mixok ? Mix382off : Xoff;
        // E-I F-H cross breaks trend
        if (eq_E_I) return slopeBAD;
        // Outer zig-zag
        if (eq_B_P && eq_A_B) return mixok ? Mix382off : Xoff;
        if (eq_D_Q && eq_A_B) return mixok ? Mix382off : Xoff;
        // Outer curve - only if opposite side forms long L-trend
        if (eq_B_P && (eq_C_F&&eq_H_S)) return mixok ? Mix382off : Xoff;
        if (eq_D_Q && (eq_F_R&&eq_G_H)) return mixok ? Mix382off : Xoff;

        return slopeBAD;
    }

    // Final cleanup for two long slopes (non-clear shapes) using relaxed rules
    // Note: Different from sim2 section - exit for solid corners
    if ( eq_B_C && eq_D_Q || eq_D_G && eq_B_P) {
        // Clear eq_A_B first to avoid corner pixel artifacts (eternal secret)
        if (eq_A_B) return theEXIT;
        return mixok ? Mix382off : Xoff;
    }

    //  A-side triple consecutive match - NEVER use without !eq_A_B !!!!!!!!!!!
    //  if (comboA3) return X+slopeOFF;

    // One more B + D bidirectional extension check (higher priority than L-shaped bars below)
    if ( (B_slope||B_tower) && (eq_D_QG&&!eq_D_G||D_tower) ) return Xoff;
    if ( (D_slope||D_tower) && (eq_B_PC&&!eq_B_C||B_tower) ) return Xoff;

    // Exit for clear L-shaped corner with inline bar (sim_X_E no exception)
    if (eq_A_B && !ThickBorder && !eq_E_I ) {

        if (B_wall && eq_E_F) return theEXIT;
        if (D_wall && eq_E_H) return theEXIT;
    }

if (sim_X_E||EalphaL) return mixok ? Mix382off : Xoff;   //xxx.alpha
    // Exit for hollow L-shaped corner with outer bar (connect inner, not outer corners)
    // Implementation: Fixes font edge cutting (Captain Commando)
    if ( (B_tower || D_tower) && (eq_E_F||eq_E_H) && !eq_A_B && !eq_E_I) return theEXIT;

    // Final B/D individual extension checks

    // Maximum reachable distance for slope
    if ( B_slope && !eq_A_B && eq(PC,CC) && noteq(PC,RC)) return mixok ? Mix382off : Xoff;
    if ( D_slope && !eq_A_B && eq(QG,GG) && noteq(QG,SG)) return mixok ? Mix382off : Xoff;

    // Slope can use fewer check points when X/E are close (with internal restrictions)
    if ( mixok && !eq_A_B ) {
        if ( B_slope && (!eq_C_F||eq(F,RC)) ) return Mix382off;
        if ( D_slope && (!eq_G_H||eq(H,SG)) ) return Mix382off;
    }

    // Exclude single bar for zig-zag below (required)
    if ((eq_E_F||eq_E_H) && !eq_E_I ) return theEXIT;

    // Zig-zag shape (eq_A_B has thickness, one side is tower (can be wall/slope), other side cannot be tower/wall (naturally prohibited))
    if ( eq_B_P && !eq_B_PA && !eq_D_Q && eq_A_B) return mixok ? Mix382off : Xoff;
    if ( eq_D_Q && !eq_D_QA && !eq_B_P && eq_A_B) return mixok ? Mix382off : Xoff;

    return theEXIT;

}   // admixX

vec4 admixS(uint A, uint B, uint C, uint D, uint E, uint F, uint G, uint H, uint I, uint R, uint RC, uint RI, uint S, uint SG, uint SI, uint II, bool eq_B_D, bool eq_E_D, float El, float Bl, vec4 colE, vec4 colF) {

            //                                    ＡＢＣ  .
            //                                  ＱＤ🄴 🅵 🆁       Zone 4
            //                      🅶 🅷 Ｉ
            //                      Ｓ
    // Implementation 1: sim E B(C) follows original logic - less jarring when E is single pixel inserted by saw
    // Implementation 2: Can E equal I? Yes!

    if (any_eq2(F,C,I)) return colE;
    //if (any_eq3(F,A,C,I)) return colE;

    if (eq(R, RI) && noteq(R,I)) return colE;
    if (eq(H, S) && noteq(H,I)) return colE;

    if ( eq(R, RC) || eq(G,SG) ) return colE;

    // Remove alpha weighting
    Bl = fract(Bl) *10.0;
    El = fract(El) *10.0;

    if ( ( eq_B_D&&eq(B,C)&&diffEB<0.381966 || eq_E_D&&sim2(colE,E,C) ) &&
    (any_eq3(I,H,S,RI) || eq(SI,RI)&&noteq(I,II)) ) return colF;

    return colE;
}

void main()
{
///////////////////////////////////////////////////////////////////////////////

    // Get actual pixel dimensions of texture (textureSize returns ivec2, convert to vec2 for float operations)
    vec2 source_size = vec2(textureSize(tex, 0));
    // Precompute inverse size for performance optimization
    vec2 inv_source_size = 1.0 / source_size;

    // Calculate fractional part of UV in pixel grid (offset within pixel)
    vec2 pos = fract(tex_coord * source_size);
    // Align UV to pixel center (core! Avoid floating point sampling errors)
    //    (pos - vec2(0.5)): Offset from UV to pixel center (-0.5~0.5)
    //    * inv_source_size: Convert pixel offset back to UV space
    //    tex_coord - offset: Final UV precisely at pixel center for stable sampling
    vec2 coord = tex_coord - (pos - vec2(0.5)) * inv_source_size;

    // ===================== Out-of-bounds check preparation: Integer pixel coordinates without floating errors =====================
    // Convert UV to float pixel coordinates (for flooring - e.g. UV(0.5,0.5) → (width/2, height/2))
    vec2 pixelPos = tex_coord * source_size; // UV to pixel coordinates (float)
    // Integer index of current pixel (0-based)
    ivec2 currPixel = ivec2(floor(pixelPos)); 
    // Convert texture size to integer vector
    ivec2 texSize = ivec2(source_size);

    #define srcf(x, y) texture(tex, coord + vec2(x, y) * inv_source_size)
    #define src(x, y) packUnorm4x8(srcf(x,y))

////////////////////////////////////////////////////////////////////////////////
    vec4 colE = srcf(0.0, 0.0);

    // Use transparent pixel if out of bounds (up/down/left/right)
    vec4 colB = (currPixel.y - 1 < 0) ? vec4(0.0) : srcf(0.0, -1.0);
    vec4 colD = (currPixel.x - 1 < 0) ? vec4(0.0) : srcf(-1.0, 0.0);
    vec4 colF = (currPixel.x + 1 >= texSize.x) ? vec4(0.0) : srcf(1.0, 0.0);
    vec4 colH = (currPixel.y + 1 >= texSize.y) ? vec4(0.0) : srcf(0.0, 1.0);

    // Default fast return to original pixel
    frag_color = colE;

    uint E = packUnorm4x8(colE);
    uint B = packUnorm4x8(colB);
    uint D = packUnorm4x8(colD);
    uint F = packUnorm4x8(colF);
    uint H = packUnorm4x8(colH);

    bool eq_E_D = eq(E,D);
    bool eq_E_F = eq(E,F);
    bool eq_E_B = eq(E,B);
    bool eq_E_H = eq(E,H);

// Skip horizontal/vertical 3x1 patterns
if (eq_E_D && eq_E_F) return;
if (eq_E_B && eq_E_H) return;

    bool eq_B_H = eq(B,H);
    bool eq_D_F = eq(D,F);
// Skip center surrounded by mirrored blocks
if ( eq_B_H && eq_D_F ) return;

// Grab 5x5 grid
uint A = src(-1.0, -1.0);
uint C = src(+1.0, -1.0);
uint G = src(-1.0, +1.0);
uint I = src(+1.0, +1.0);

uint P  = src(+0.0, -2.0);
uint Q  = src(-2.0, +0.0);
uint R  = src(+2.0, +0.0);
uint S  = src(+0.0, +2.0);

uint PA = src(-1.0, -2.0);
uint PC = src(+1.0, -2.0);
uint QA = src(-2.0, -1.0);
uint QG = src(-2.0, +1.0); //             AA    PA    [P]   PC    CC
uint RC = src(+2.0, -1.0); //                ┌──┬──┬──┐
uint RI = src(+2.0, +1.0); //             QA │  A │  B │ C  │ RC
uint SG = src(-1.0, +2.0); //                ├──┼──┼──┤
uint SI = src(+1.0, +2.0); //            [Q] │  D │  E │ F  │ [R]
uint AA = src(-2.0, -2.0); //                ├──┼──┼──┤
uint CC = src(+2.0, -2.0); //             QG │  G │  H │ I  │ RI
uint GG = src(-2.0, +2.0); //                └──┴──┴──┘
uint II = src(+2.0, +2.0); //             GG    SG    [S]   SI    II

// Default to nearest-neighbor scaling
    vec4 J = colE;    vec4 K = colE;    vec4 L = colE;    vec4 M = colE;

    float Bl = luma(colB), Dl = luma(colD), El = luma(colE), Fl = luma(colF), Hl = luma(colH);

// 1:1 slope rules (P95)
    bool eq_B_D = eq(B,D);
    bool eq_B_F = eq(B,F);
    bool eq_D_H = eq(D,H);
    bool eq_F_H = eq(F,H);

    // Any mirrored blocks surrounding center
    bool oppoPix =  eq_B_H || eq_D_F;
    // Flag for admixX function entry via 1:1 slope rules
    bool slope1 = false;    bool slope2 = false;    bool slope3 = false;    bool slope4 = false;
    // Flag for valid pixel return via 1:1 slope rules
    bool slope1ok = false;  bool slope2ok = false;  bool slope3ok = false;  bool slope4ok = false;
    // slopeBAD: entered admixX but returned E for at least one of JKLM
    // slopOFF: returned with OFF flag - no long slope calculation

    // B - D
    if ( (!eq_E_B&&!eq_E_D&&!oppoPix) && (!eq_D_H&&!eq_B_F) && (El>=Dl&&El>=Bl || eq(E,A)) && ( (El<Dl&&El<Bl) || none_eq2(A,B,D) || noteq(E,P) || noteq(E,Q) ) && ( eq_B_D&&(eq_F_H||eq(E,A)||eq(B,PC)||eq(D,QG)) || sim1(colB,colD)&&(sim2(colE,E,C)||sim2(colE,E,G)) ) ) {
        J=admixX(A,B,C,D,E,F,G,H,I,P,PA,PC,Q,QA,QG,R,RC,RI,S,SG,SI,AA,CC,GG, El,Bl,Dl,Fl,Hl,colE,colB,colD);
        slope1 = true;
        if (J.b > 1.0 ) {
            if (J.b > 7.0 ) J=J-8.0;     //slopOFF
            if (J.b == 4.0 ) return;
            if (J.b == 2.0 ) J=colE;     //slopeBAD
        } else slope1ok = true;
    }
    // B - F
    if ( !slope1 && (!eq_E_B&&!eq_E_F&&!oppoPix) && (!eq_B_D&&!eq_F_H) && (El>=Bl&&El>=Fl || eq(E,C)) && ( (El<Bl&&El<Fl) || none_eq2(C,B,F) || noteq(E,P) || noteq(E,R) ) && ( eq_B_F&&(eq_D_H||eq(E,C)||eq(B,PA)||eq(F,RI)) || sim1(colB,colF)&&(sim2(colE,E,A)||sim2(colE,E,I)) ) )  {
        K=admixX(C,F,I,B,E,H,A,D,G,R,RC,RI,P,PC,PA,S,SI,SG,Q,QA,QG,CC,II,AA, El,Fl,Bl,Hl,Dl,colE,colF,colB);
        slope2 = true;
        if (K.b > 1.0 ) {
            if (K.b > 7.0 ) K=K-8.0;
            if (K.b == 4.0 ) return;
            if (K.b == 2.0 ) K=colE;
        } else {slope2ok = true;}
    }
    // D - H
    if ( !slope1 && (!eq_E_D&&!eq_E_H&&!oppoPix) && (!eq_F_H&&!eq_B_D) && (El>=Hl&&El>=Dl || eq(E,G))  &&  ((El<Hl&&El<Dl) || none_eq2(G,D,H) || noteq(E,S) || noteq(E,Q))  &&  ( eq_D_H&&(eq_B_F||eq(E,G)||eq(D,QA)||eq(H,SI)) || sim1(colD,colH) && (sim2(colE,E,A)||sim2(colE,E,I)) ) )  {
        L=admixX(G,D,A,H,E,B,I,F,C,Q,QG,QA,S,SG,SI,P,PA,PC,R,RI,RC,GG,AA,II, El,Dl,Hl,Bl,Fl,colE,colD,colH);
        slope3 = true;
        if (L.b > 1.0 ) {
            if (L.b > 7.0 ) L=L-8.0;
            if (L.b == 4.0 ) return;
            if (L.b == 2.0 ) L=colE;
        } else {slope3ok = true;}
    }
    // F - H
    if ( !slope2 && !slope3 && (!eq_E_F&&!eq_E_H&&!oppoPix) && (!eq_B_F&&!eq_D_H) && (El>=Fl&&El>=Hl || eq(E,I))  &&  ((El<Fl&&El<Hl) || none_eq2(I,F,H) || noteq(E,R) || noteq(E,S))  &&  ( eq_F_H&&(eq_B_D||eq(E,I)||eq(F,RC)||eq(H,SG)) || sim1(colF,colH) && (sim2(colE,E,C)||sim2(colE,E,G)) ) )  {
        M=admixX(I,H,G,F,E,D,C,B,A,S,SI,SG,R,RI,RC,Q,QG,QA,P,PC,PA,II,GG,CC, El,Hl,Fl,Dl,Bl,colE,colH,colF);
        slope4 = true;
        if (M.b > 1.0 ) {
            if (M.b > 7.0 ) M=M-8.0;
            if (M.b == 4.0 ) return;
            if (M.b == 2.0 ) M=colE;
        } else {slope4ok = true;}
    }

//  long gentle 2:1 slope  (P100)

    bool longslope = false;

    if (slope4ok && eq_F_H) { //zone4 long slope
        // Original rule extension 1: Pass adjacent pixel to admixL for comparison to prevent double mixing
        // Original rule extension 2: No L-shape within opposite pixel interval unless wall formed
        if (eq(G,H) && eq(F,R) && noteq(R, RC) && (noteq(Q,G)||eq(Q, QA))) {L=admixL(M,L,colH); longslope = true;}
        // vertical
        if (eq(C,F) && eq(H,S) && noteq(S, SG) && (noteq(P,C)||eq(P, PA))) {K=admixL(M,K,colF); longslope = true;}
    }

    if (slope3ok && eq_D_H) { //zone3 long slope
        // horizontal
        if (eq(D,Q) && eq(H,I) && noteq(Q, QA) && (noteq(R,I)||eq(R, RC))) {M=admixL(L,M,colH); longslope = true;}
        // vertical
        if (eq(A,D) && eq(H,S) && noteq(S, SI) && (noteq(A,P)||eq(P, PC))) {J=admixL(L,J,colD); longslope = true;}
    }

    if (slope2ok && eq_B_F) { //zone2 long slope
        // horizontal
        if (eq(A,B) && eq(F,R) && noteq(R, RI) && (noteq(A,Q)||eq(Q, QG))) {J=admixL(K,J,colB); longslope = true;}
        // vertical
        if (eq(F,I) && eq(B,P) && noteq(P, PA) && (noteq(I,S)||eq(S, SG))) {M=admixL(K,M,colF); longslope = true;}
    }

    if (slope1ok && eq_B_D) { //zone1 long slope
        // horizontal
        if (eq(B,C) && eq(D,Q) && noteq(Q, QG) && (noteq(C,R)||eq(R, RI))) {K=admixL(J,K,colB); longslope = true;}
        // vertical
        if (eq(D,G) && eq(B,P) && noteq(P, PC) && (noteq(G,S)||eq(S, SI))) {L=admixL(J,L,colD); longslope = true;}
    }

// Exit if longslope formed (rarely forms sawslope diagonally)
bool skiprest = longslope;

bool slopeok = slope1ok||slope2ok||slope3ok||slope4ok;

// Note: sawslope cannot exclude slopOFF (few cases) and slopeBAD (very rare), but can exclude slopeok (strong shapes)
if (!skiprest && !oppoPix && !slopeok) {

        // horizontal bottom
        if (!eq_E_H && none_eq2(H,A,C)) {

            //                                    A B Ｃ ・
            //                                  Q D 🄴 🅵 🆁       Zone 4
            //                      🅶 🅷 I
            //                      Ｓ
            // (!slope3 && !eq_D_H) combination is better
            if ( (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H)  && !eq_F_H &&
                !eq_E_F && (eq_B_D || eq_E_D) && eq(R,H) && eq(F,G) ) {
                M = admixS(A,B,C,D,E,F,G,H,I,R,RC,RI,S,SG,SI,II,eq_B_D,eq_E_D,El,Bl,colE,colF);
                skiprest = true;}

            //                                  ・  A Ｂ C
            //                                  🆀 🅳 🄴 Ｆ R       Zone 3
            //                                     G 🅷 🅸
            //                       Ｓ
            if ( !skiprest && (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && !eq_D_H &&
                 !eq_E_D && (eq_B_F || eq_E_F) && eq(Q,H) && eq(D,I) ) {
                L = admixS(C,B,A,F,E,D,I,H,G,Q,QA,QG,S,SI,SG,GG,eq_B_F,eq_E_F,El,Bl,colE,colD);
                skiprest = true;}
        }

        // horizontal up
        if ( !skiprest && !eq_E_B && none_eq2(B,G,I)) {

            //                       Ｐ
            //                                    🅐 🅑 Ｃ
            //                                  ＱＤ 🄴 🅵 🆁       Zone 2
            //                                    Ｇ H  I  .
            if ( (!slope1 && !eq_B_D)  && (!slope4 && !eq_F_H) && !eq_B_F &&
                  !eq_E_F && (eq_D_H || eq_E_D) && eq(B,R) && eq(A,F) ) {
                K = admixS(G,H,I,D,E,F,A,B,C,R,RI,RC,P,PA,PC,CC,eq_D_H,eq_E_D,El,Hl,colE,colF);
                skiprest = true;}

            //                      Ｐ
            //                                    A 🅑 🅲
            //                                 🆀 🅳 🄴 Ｆ R        Zone 1
            //                                  . G Ｈ I
            if ( !skiprest && (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H) && !eq_B_D &&
                 !eq_E_D && (eq_F_H || eq_E_F) && eq(B,Q) && eq(C,D) ) {
                J = admixS(I,H,G,F,E,D,C,B,A,Q,QG,QA,P,PC,PA,AA,eq_F_H,eq_E_F,El,Hl,colE,colD);
                skiprest = true;}

        }

        // vertical left
        if ( !skiprest && !eq_E_D && none_eq2(D,C,I) ) {

            //                                    🅐 B Ｃ
            //                                  Q 🅳 🄴 Ｆ R
            //                                    Ｇ 🅷 I        Zone 3
            //                                       🆂 ・
            if ( (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && !eq_D_H &&
                  !eq_E_H && (eq_B_F || eq_E_B) && eq(D,S) && eq(A,H) ) {
                L = admixS(C,F,I,B,E,H,A,D,G,S,SI,SG,Q,QA,QG,GG,eq_B_F,eq_E_B,El,Fl,colE,colH);
                skiprest = true;}

            //                                      🅟 ・
            //                                    A 🅑 C
            //                                  Q 🅳 🄴 F R       Zone 1
            //                                    🅶 ＨＩ
            if ( !skiprest && (!slope3 && !eq_D_H) && (!slope2 && !eq_B_F) && !eq_B_D &&
                  !eq_E_B && (eq_F_H || eq_E_H) && eq(P,D) && eq(B,G) ) {
                J = admixS(I,F,C,H,E,B,G,D,A,P,PC,PA,Q,QG,QA,AA,eq_F_H,eq_E_H,El,Fl,colE,colB);
                skiprest = true;}

        }

        // vertical right
        if ( !skiprest && !eq_E_F && none_eq2(F,A,G) ) { // right

            //                                    A B 🅲
            //                                  Q D 🄴 🅵 R
            //                                    G 🅷 I        Zone 4
            //                                    . 🆂
            if ( (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H) && !eq_F_H &&
                  !eq_E_H && (eq_B_D || eq_E_B) && eq(S,F) && eq(H,C) ) {
                M = admixS(A,D,G,B,E,H,C,F,I,S,SG,SI,R,RC,RI,II,eq_B_D,eq_E_B,El,Dl,colE,colH);
                skiprest = true;}

            //                                    ・ 🅟
            //                                    A 🅑 C
            //                                  Q D 🄴 🅵 R        Zone 2
            //                                    G H 🅸
            if ( !skiprest && (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && !eq_B_F &&
                 !eq_E_B && (eq_D_H || eq_E_H) && eq(P,F) && eq(B,I) ) {
                K = admixS(G,D,A,H,E,B,I,F,C,P,PA,PC,R,RI,RC,CC,eq_D_H,eq_E_H,El,Dl,colE,colB);
                skiprest = true;}

        } // vertical right
} // sawslope

// Exit if sawslope formed - legacy: skiprest||slopeBAD (also uses slopOFF (weak) and slopok (strong) with poor results)
skiprest = skiprest||slope1||slope2||slope3||slope4||El>7.0||Bl>7.0||Dl>7.0||Fl>7.0||Hl>7.0;

/**************************************************
       Concave + Cross shape	（P100）	   
 *************************************************/
//  Using approximate pixels for cross far ends helps with horizontal line + sawtooth and layered gradients
//  e.g. SFIII: 3rd Strike intro glow text, SFZ3 Mix Japanese buildings, Garou: Mark of the Wolves intro

    vec4 colX;        // Temporary X definition

    if (!skiprest &&
        Bl<El && !eq_E_D && !eq_E_F && eq_E_H && none_eq2(E,A,C) && all_eq2(G,H,I) && sim2(colE,E,S) ) { // TOP

        if (eq_B_D||eq_B_F) { J=admixK(colB,J);    K=J;
            if (eq_D_F) { L=mix(J,L, 0.61804);   M=L; }
        } else { colX = El-Bl < abs(El-Dl) ? colB : colD;  J=admixC(colX,J);
			if (eq_D_F) { K=J;  L=mix(J,L, 0.61804);    M=L; }
			else {colX = El-Bl < abs(El-Fl) ? colB : colF; 		K=admixC(colX,K); }
            }

	   skiprest = true;
	}

    if (!skiprest &&
		Hl<El && !eq_E_D && !eq_E_F && eq_E_B && none_eq2(E,G,I) && all_eq2(A,B,C) && sim2(colE,E,P) ) { // BOTTOM

        if (eq_D_H||eq_F_H) { L=admixK(colH,L);    M=L;
            if (eq_D_F) { J=mix(L,J, 0.61804);   K=J; }
        } else { colX = El-Hl < abs(El-Dl) ? colH : colD;  L=admixC(colX,L);
			if (eq_D_F) { M=L;  J=mix(L,J, 0.61804);    K=J; }
			else { colX = El-Hl < abs(El-Fl) ? colH : colF;    M=admixC(colX,M); }
            }

	   skiprest = true;
	}

   if (!skiprest &&
		Fl<El && !eq_E_B && !eq_E_H && eq_E_D && none_eq2(E,C,I) && all_eq2(A,D,G) && sim2(colE,E,Q) ) { // RIGHT

        if (eq_B_F||eq_F_H) { K=admixK(colF,K);    M=K;
            if (eq_B_H) { J=mix(K,J, 0.61804);   L=J; }
        } else { colX = El-Fl < abs(El-Bl) ? colF : colB;  K=admixC(colX,K);
			if (eq_B_H) { M=K;  J=mix(K,J, 0.61804);    L=J; }
			else { colX = El-Fl < abs(El-Hl) ? colF : colH;    M=admixC(colX,M); }
            }

	   skiprest = true;
	}

    if (!skiprest &&
		Dl<El && !eq_E_B && !eq_E_H && eq_E_F && none_eq2(E,A,G) && all_eq2(C,F,I) && sim2(colE,E,R) ) { // LEFT

        if (eq_B_D||eq_D_H) { J=admixK(colD,J);    L=J;
            if (eq_B_H) { K=mix(J,K, 0.61804);   M=K; }
        } else { colX = El-Dl < abs(El-Bl) ? colD : colB;  J=admixC(colX,J);
			if (eq_B_H) { L=J;   K=mix(J,K, 0.61804);    M=K; }
			else { colX = El-Dl < abs(El-Hl) ? colD : colH;    L=admixC(colX,L); }
            }

	   skiprest = true;
	}

/*
        ✕О
    ООО✕
        ✕О    Scorpion Type (P99). It looks a lot like the tracking bug from The Matrix. 
        Can match some regularly interleaved pixels.
*/
// Practice Notes: 1. Using approximations for the scorpion's pincers? 
// This is prone to causing graphical glitches.
// Practice Notes: 2. Removing one grid cell from the scorpion's tail captures more patterns.
// Among the four patterns, only the scorpion type is exclusive — 
// once a previous rule has captured it (i.e., it has been matched), this pattern will not appear.


   if (!skiprest && !eq_E_F &&eq_E_D&&eq_B_F&&eq_F_H && all_eq2(E,C,I) && noteq(F,src(+3.0, 0.0)) ) {K=admixK(colF,K); M=K;J=mix(K,J, 0.61804); L=J;skiprest=true;}	// RIGHT
   if (!skiprest && !eq_E_D &&eq_E_F&&eq_B_D&&eq_D_H && all_eq2(E,A,G) && noteq(D,src(-3.0, 0.0)) ) {J=admixK(colD,J); L=J;K=mix(J,K, 0.61804); M=K;skiprest=true;}	// LEFT
   if (!skiprest && !eq_E_H &&eq_E_B&&eq_D_H&&eq_F_H && all_eq2(E,G,I) && noteq(H,src(0.0, +3.0)) ) {L=admixK(colH,L); M=L;J=mix(L,J, 0.61804); K=J;skiprest=true;}	// BOTTOM
   if (!skiprest && !eq_E_B &&eq_E_H&&eq_B_D&&eq_B_F && all_eq2(E,A,C) && noteq(B,src(0.0, -3.0)) ) {J=admixK(colB,J); K=J;L=mix(J,L, 0.61804); M=L;}				// TOP

	//final write
    frag_color = (pos.x < 0.5) ? (pos.y < 0.5 ? J : L) : (pos.y < 0.5 ? K : M);
}
