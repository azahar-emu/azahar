// Copyright 2023 Citra Emulator Project
// 2025 Enhanced by CrashGG.
// Licensed under GPLv2 or any later version
// Refer to the license.txt included.

//? #version 430 core
precision highp float;

layout(location = 0) in vec2 tex_coord;
layout(location = 0) out vec4 frag_color;
layout(binding = 0) uniform sampler2D tex;
///////////////////////////////////////////

// Luma with RGB visual weights + alpha tier weighting
float luma(vec4 col) {

	// Use CRT-era BT.601 standard. Result divided by 10, range [0.0 - 0.1]
    float rgbsum =dot(col.rgb, vec3(0.0299, 0.0587, 0.0114));

	// Multiply by 10 later to remove alpha weighting
    float alphafactor = 
        (col.a > 0.854102) ? 1.0 :		// Upper segment of 2 short golden ratios
        (col.a > 0.618034) ? 2.0 :		// 1 golden ratio
        (col.a > 0.381966) ? 3.0 :		// 1 short golden ratio
        (col.a > 0.145898) ? 4.0 :		// 2 short golden ratios
        (col.a > 0.002) ? 5.0 : 8.0;	// Fully transparent

    return rgbsum + alphafactor;

}

/* Constant definitions:
0.145898	:			2 short golden ratios of 1.0
0.0638587	:		Squared Euclidean distance after 2 short golden ratios
0.024391856	:		Squared Euclidean distance after 2 short + 1 golden ratio
0.00931686	:		Squared Euclidean distance after 3 short golden ratios
0.001359312	:		Squared Euclidean distance after 4 short golden ratios
0.4377		:		Squared Euclidean distance after 1 short golden ratio
0.75			:		Squared half Euclidean distance of RGB
*/
// Pixel similarity check LV1
// pin zz
bool sim1(vec4 col1, vec4 col2) {

    vec4 diff = col1 - col2;
    vec4 absdiff = abs(diff);

    // 1. Fast channel difference check
    if ( absdiff.r > 0.1 || absdiff.g > 0.1 || absdiff.b > 0.1 || absdiff.a > 0.145898 ) return false;

    // 2. Fast squared distance check
	float dot_diff = dot(diff.rgb, diff.rgb);
    if (dot_diff < 0.001359312) return true;

    // 3. Gradient pixel check
    float min_diff = min(diff.r, min(diff.g, diff.b));
    float max_diff = max(diff.r, max(diff.g, diff.b));
    if ( max_diff-min_diff>0.096 ) return false;    // Exit if difference exceeds int24
    if ( max_diff-min_diff<0.024 && dot_diff<0.024391856)  return true;  // Treat as gradient if < int6, loosen threshold

	// 4. Gray pixel check
    float sum1 = dot(col1.rgb, vec3(1.0));  // Sum RGB channels
    float sum2 = dot(col2.rgb, vec3(1.0));
    float avg1 = sum1 * 0.3333333;
    float avg2 = sum2 * 0.3333333;

	vec3 graydiff1 = col1.rgb - vec3(avg1);
	vec3 graydiff2 = col2.rgb - vec3(avg2);
	float dotgray1 = dot(graydiff1,graydiff1);
	float dotgray2 = dot(graydiff2,graydiff2);
    // 0.002: Allow single-channel diff up to int13 at avg=20; 0.0004: up to int6 single / int3+4 dual
	float tolerance1 = avg1<0.08 ? 0.002 : 0.0004;
	float tolerance2 = avg2<0.08 ? 0.002 : 0.0004;
    // 0.078: Limit green max to 19, human eye perception threshold
    bool Col1isGray = sum1<0.078||dotgray1<tolerance1;
    bool Col2isGray = sum2<0.078||dotgray2<tolerance2;

	// Loosen to Lv2 if both are gray
    if ( Col1isGray && Col2isGray && dot_diff<0.024391856 ) return true;

	// Exit if only one is gray
    if ( Col1isGray != Col2isGray ) return false;

    // Accumulate positive/negative separately with max/min
    float team_pos = abs(dot(max(diff.rgb, 0.0), vec3(1.0)));
    float team_neg = abs(dot(min(diff.rgb, 0.0), vec3(1.0)));
    // Find opposing channels
    float team_rebel = min(team_pos, team_neg);
    // Test requires 3x opposing value (1x neutral, 2x upward trend)
    return dot_diff + team_rebel*team_rebel*3.0 < 0.00931686;

}

float rgbDist(vec3 col1, vec3 col2) {

    // 1. Clamp dark regions
    vec3 clampCol1 = max(col1, vec3(0.078));
    vec3 clampCol2 = max(col2, vec3(0.078));

    vec3 clampdiff = clampCol1 - clampCol2;

    return dot(clampdiff, clampdiff);
}

// Pixel similarity check Lv2 Lv3
bool vi_sim2(vec4 colC1, uint C1, uint C2) {
    if (C1==C2) return true;
	vec4 colC2 = unpackUnorm4x8(C2);
	// Ignore RGB if both near transparent
	if ( colC1.a < 0.381966 && colC2.a < 0.381966 ) return true;
	// Alpha difference cannot exceed 0.382
	if ( abs(colC1.a-colC2.a)>0.381966) return false;

    return rgbDist(colC1.rgb, colC2.rgb) < 0.024391856;
}

bool sim2(vec4 col1, vec4 col2) {
	// Ignore RGB if both near transparent
	if ( col1.a < 0.381966 && col2.a < 0.381966 ) return true;
	// Alpha difference cannot exceed 0.382
	if ( abs(col1.a-col2.a)>0.381966) return false;
    return rgbDist(col1.rgb, col2.rgb) < 0.024391856;
}

bool sim3(vec4 col1, vec4 col2) {
	// Ignore RGB if both near transparent
	if ( col1.a < 0.145898 && col2.a < 0.145898 ) return true;
	// Alpha difference cannot exceed 0.382
	if ( abs(col1.a-col2.a)>0.381966) return false;
    return rgbDist(col1.rgb, col2.rgb) < 0.0638587;
}

bool mixcheck(vec4 col1, vec4 col2) {

	// Transparent pixels filtered externally
	vec4 diff = col1 - col2;
	// No mixing if alpha diff > 50%
	if ( abs(diff.a) > 0.5 ) return false;

    // Gradient pixel check
    float min_diff = min(diff.r, min(diff.g, diff.b));
    float max_diff = max(diff.r, max(diff.g, diff.b));
    if ( max_diff-min_diff>0.618034 ) return false;

	float dot_diff = dot(diff.rgb, diff.rgb);
    if( max_diff-min_diff<0.024 && dot_diff<0.75)  return true;  // < int5 / int6

    // Accumulate positive/negative separately with max/min
    float team_pos = abs(dot(max(diff.rgb, 0.0), vec3(1.0)));
    float team_neg = abs(dot(min(diff.rgb, 0.0), vec3(1.0)));
    // Add opposing channels to squared distance for check
    float team_rebel = min(team_pos, team_neg);
    // Test requires 3x opposing value
    return dot_diff + team_rebel*team_rebel*3.0 < 0.4377;
}

// RGB must match, small alpha difference allowed
bool eq(uint C1, uint C2){
    if (C1 == C2) return true;

	uint rgbC1 = C1 & 0x00FFFFFFu;
	uint rgbC2 = C2 & 0x00FFFFFFu;

	if (rgbC1 != rgbC2) return false;

    uint alphaC1 = C1 >> 24;
    uint alphaC2 = C2 >> 24;

    // Note: uint cannot use abs(alphaC1-alphaC2)
    uint alphaDiff = (alphaC1 > alphaC2) ? (alphaC1 - alphaC2) : (alphaC2 - alphaC1);

    return alphaDiff < 38u;	// 2 short golden ratios of 255

}

#define noteq(a,b) (a!=b)

bool vec_noteq(vec4 col1, vec4 col2) {
    vec4 diff = abs(col1 - col2);
	// Allow total RGB diff int2, alpha diff int5
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
// Concave + Cross pattern (weak blending / none)
vec4 admixC(vec4 colX, vec4 colE) {
	// Transparent pixels filtered in main

	bool mixok = mixcheck(colX, colE);

	return mixok ? Mix618 : colE;

}

// K-pattern: Forced weak blending (weaker)
vec4 admixK(vec4 colX, vec4 colE) {
	// Transparent pixels filtered in main

	bool mixok = mixcheck(colX, colE);

	return mixok ? Mix618 : Mix854;

}

// L-pattern: 2:1 slope, main corner extension
// Practice: This rule requires 4 identical pixels on strict slope, otherwise artifacts appear
vec4 admixL(vec4 colX, vec4 colE, vec4 colS) {

    // eqX,E check originally caught many duplicates; now filtered by slopeok in main

	// Copy if target X != sample S (already blended once), no re-blend
	if (vec_noteq(colX, colS)) return colX;

	bool mixok = mixcheck(colX, colE);

    return mixok ? Mix382 : colX;
}

/**************************************************************************************************************************************
 * 												main slope + X cross-processing mechanism						                *
 ******************************************************************************************************************************** zz  */
vec4 admixX(uint A, uint B, uint C, uint D, uint E, uint F, uint G, uint H, uint I,
			uint P, uint PA, uint PC, uint Q, uint QA, uint QG, uint R, uint RC, uint RI, uint S, uint SG, uint SI, uint AA, uint CC, uint GG,
			float El, float Bl, float Dl, float Fl, float Hl,
			vec4 colE, vec4 colB, vec4 colD, vec4 colC, vec4 colG,
			bool eq_B_D, bool eq_F_H, bool eq_E_A, bool eq_E_C, bool eq_E_G, bool eq_E_I, bool eq_E_F, bool eq_E_H) {


	bool eq_B_C = eq(B, C);
	bool eq_D_G = eq(D, G);

    // Exit if double straight walls sandwich
    if (eq_B_C && eq_D_G) return slopeBAD;

	// Pre-declare
	bool eq_A_B;		bool eq_A_D;		bool eq_A_P;		bool eq_A_Q;
	bool eq_B_P;		bool eq_B_PA;	bool eq_B_PC;
	bool eq_D_Q;		bool eq_D_QA;	bool eq_D_QG;
	bool B_slope;	bool B_tower;	bool B_wall;
    bool D_slope;	bool D_tower;	bool D_wall;
	vec4 colX;		bool Xisblack;
    bool mixok;
	bool comboA3;    bool En3;
	
    #define En4square  En3 && eq_E_I

	// E is near-transparent (not fully), mostly edge pixels
	bool EalphaL = colE.a >0.002 && colE.a <0.381966;

	// Remove alpha channel weighting
	Bl = fract(Bl) *10.0;
	Dl = fract(Dl) *10.0;
	El = fract(El) *10.0;
	Fl = fract(Fl) *10.0;
	Hl = fract(Hl) *10.0;

/*=========================================
                    B != D
  ==================================== zz */
if (!eq_B_D){

	// Exit if E==A (invalid logic)
	if (eq_E_A) return slopeBAD;

	// Exit if BD difference > either BE/DE
	float diffBD = abs(Bl-Dl);
	if (diffBD > diffEB || diffBD > diffED) return slopeBAD;

	// Prevent font single-pixel edges crushed by black background (brightness diff >0.5)
	// Note: Must check both black if BD !=
	Xisblack = checkblack(colB) && checkblack(colD);
	if ( Xisblack && El >0.5 && (Fl<0.078 || Hl<0.078) ) return theEXIT;

// Exclusion rule before original (triangle vertex cannot protrude)
	eq_A_B = eq(A,B);
	if ( !Xisblack && eq_A_B && eq_D_G && eq(B,P) ) return slopeBAD;

	eq_A_D = eq(A,D);
	if ( !Xisblack && eq_A_D && eq_B_C && eq(D,Q) ) return slopeBAD;

    // B/D isolated? Not applicable here (fixes some bugs but loses shapes, e.g. Double Dragon attract mode sprites)

	// X = B/D mix
	colX = mix(colB, colD, 0.5);
	colX.a = min(colB.a, colD.a);

	mixok = E!=0u && mixcheck(colX,colE);

	eq_A_P = eq(A,P);
	eq_A_Q = eq(A,Q);
	comboA3 = eq_A_P && eq_A_Q;
	// High priority: A-side triple alignment
    if (comboA3) return mixok ? Mix382off : Xoff;

    // Official original rule
    if ( eq_E_C || eq_E_G ) return mixok ? Mix382off : Xoff;

    // Original rule enhancement 1: Good trend capture, enhancement 2 disabled (wall bypass issue)
    if ( !eq_D_G&&eq(E,QG)&&sim2(colE,colG) || !eq_B_C&&eq(E,PC)&&sim2(colE,colC) ) return mixok ? Mix382off : Xoff;


    // Exclude 3-pixel single-side walls
    if (!Xisblack){
        if ( eq_A_B&&eq_B_C || eq_A_D&&eq_D_G ) return slopeBAD;
    }

	// Near-transparent E edge
    if (EalphaL) return mixok ? Mix382off : Xoff;

    // F-H inline trend (includes En3), blocked by 3-pixel wall rule
    if ( eq_F_H ) return mixok ? Mix382off : Xoff;

    // Abandon remaining 2-pixel walls and isolated pixels
    return slopeBAD;
} // B != D

/*******  B == D prepare *******/
  
	// Prevent font edges crushed by black background on 3 sides
	Xisblack = checkblack(colB);
	if ( Xisblack && El >0.5 && (Fl<0.078 || Hl<0.078) ) return theEXIT;

    colX = colB;
	colX.a = min(colB.a , colD.a);

	float distEC = float(!eq_E_C);
	float distEG = float(!eq_E_G);

	if (!eq_E_C) {
		if ( colE.a<0.381966 && colC.a<0.381966 ) {	// Ignore RGB if both near transparent
			distEC = 0.0;
		} else if (abs(colE.a-colC.a)<0.381966) distEC = rgbDist(colE.rgb, colC.rgb);
	}
	if (!eq_E_G) {
		if ( colE.a<0.381966 && colG.a<0.381966 ) {	// Ignore RGB if both near transparent
			distEC = 0.0;
		} else if (abs(colE.a-colG.a)<0.381966) distEG = rgbDist(colE.rgb, colG.rgb);
	}

	bool sim_EC = distEC < 0.024391856;
	bool sim_EG = distEG < 0.024391856;

	bool comboE3 = eq_E_C && eq_E_G;
	bool ThickBorder;


/*===============================================
                 Original core rule with sim2 enhancement
  ========================================== zz */
if ( (sim_EC || sim_EG) && !eq_E_A ){

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


    // Strong shape: B/D continuous extension
    if ( (B_slope||B_tower) && (D_slope||D_tower) && !eq_A_B) return Xoff;

	mixok = E!=0u && mixcheck(colX,colE);

	eq_A_P = eq(A, P);
	eq_A_Q = eq(A, Q);
	comboA3 = eq_A_P && eq_A_Q;

    ThickBorder = eq_A_B && (eq_A_P||eq_A_Q|| eq(A,AA)&&(eq_B_PA||eq_D_QA));

	if (ThickBorder && !Xisblack) mixok=false;

    // A-side triple alignment
    if (comboA3) {
        if (!eq_A_B) return Xoff;	// Hollow: strong shape return
        else mixok=false;			// Solid: no blending
    }

	// E-side triple alignment
	if (comboE3) return mixok ? Mix382off : Xoff;


	B_wall = eq_B_C && !eq_B_PC && !eq_B_P;
	D_wall = eq_D_G && !eq_D_QG && !eq_D_Q;

    // Clear long slope (non-thick edge, strong trend!)
    // Special handling for long slopes
	if ( B_wall && D_tower ) {
        if (eq_E_G || sim_EG&&eq(E,QG) ) {   // Original + enhancement
            if (eq_A_B) return mixok ? Mix382 : colX;    // Thick
            return colX;                               // Hollow
        }
        if (eq_A_B) return slopeBAD;
        // 2-pixel with long slope follow
        if (eq_E_F ) return colX;
        // 1-pixel no long slope
        return Xoff;
    }

	if ( B_tower && D_wall ) {
        if (eq_E_C || sim_EC&&eq(E,PC) ) {   // Original + enhancement
            if (eq_A_B) return mixok ? Mix382 : colX;    // Thick
            return colX;                                // Hollow
        }
        if (eq_A_B) return slopeBAD;
        // 2-pixel with long slope follow
        if (eq_E_H ) return colX;
        // 1-pixel no long slope
        return Xoff;
    }


    // Official original (after special shapes: no blend for strong forms)
	// Fix for original rule
    if (eq_E_C ) {
		if (eq_A_B && eq_B_PA && !eq_B_PC && eq_E_F && eq(E,P)) return theEXIT;
		return mixok ? Mix382 : colX;
	}

	if (eq_E_G) {
		if (eq_A_B && eq_D_QA && !eq_D_QG && eq_E_H && eq(E,Q)) return theEXIT;
		return mixok ? Mix382 : colX;
	}

    // Original rule enhancement 1
    if (sim_EG&&!eq_D_G&&eq(E,QG) || sim_EC&&!eq_B_C&&eq(E,PC)) return mixok ? Mix382off : Xoff;

    // Original rule enhancement 2
    if (sim_EC && sim_EG) return mixok ? Mix382off : Xoff;


    // F-H inline trend (skip En4/En3)
    if ( eq_F_H )  return mixok ? Mix382off : Xoff;

	// Cleanup last 2 long slopes (non-clear shapes)
    // Practice: Different from sim2 cleanup, cube exits
	if (eq_B_C && eq_D_Q) {
        // Double cube exit
		if (eq_B_P && eq_B_PC && eq_A_B && eq_D_QA && !eq_D_QG && eq_E_F && eq(H,I) ) return theEXIT;

		return mixok ? Mix382off : Xoff;
	}

	if ( eq_D_G && eq_B_P) {
        // Double cube exit
		if (eq_D_Q && eq_D_QG && eq_A_B && eq_B_PA && !eq_B_PC && eq_E_H && eq(F,I) ) return theEXIT;

		return mixok ? Mix382off : Xoff;
	}


    // Exit: L-notch clear inner parallel line
    if (eq_A_B && !ThickBorder && !eq_E_I ) {
	    if (B_wall && eq_E_F) return theEXIT;
	    if (D_wall && eq_E_H) return theEXIT;
	}

    // Early return if similar color
    if (mixok) return Mix382off;
    if (EalphaL) return mixok ? Mix382off : Xoff;

    // Exit: L-notch hollow outer line (fixes font edges)
    if ( !eq_A_B && (eq_E_F||eq_E_H) && !eq_E_I) {
        if (B_tower && !eq_D_Q && !eq_D_QG) return theEXIT;
        if (D_tower && !eq_B_P && !eq_B_PC) return theEXIT;
    }

    // Fallback
    return mixok ? Mix382off : Xoff;

} // sim2 base


/*===================================================
                    E - A Cross
  ============================================== zz */
if (eq_E_A) {

	// Cross judgment needs "region" and "trend" concepts, tight conditions for different regions

    // B/D isolated exit? Not needed here!


    En3 = eq_E_F&&eq_E_H;

	// Special: Square (En4square)
	if ( En4square ) {
        if( noteq(G,H) && noteq(C,F)                      // Independent clear 4/6-pixel rectangle (both sides)
		&& (eq(H,S) == eq(I,SI) && eq(F,R) == eq(I,RI)) ) return theEXIT;
        // else return mixok ? mix(X,E,0.381966) : X;  Must enter checkerboard rule (adjacent B/D may form bubbles)
    }

    // Special: Dithering pattern
	// Practice 1: Use !eq_E_F/!eq_E_H not !sim (loses shapes)
    // Practice 2: Force blend (prevents shape loss: Power Instinct horse, Slam Masters 2)

	bool Eisblack = checkblack(colE);
	mixok = E!=0u && mixcheck(colX,colE);

	// 1. Dithering center
    if ( comboE3 && eq_E_I && !eq_E_F && !eq_E_H ) {

		// Exit if E is black (KOF 96 power gauge, Punisher belt: avoid high-contrast blend)
		if (Eisblack) return theEXIT;
		// Practice: No black B check (normal logic)
		// Practice: No separate F-H check (95%+), gradient gauge fallback
		return mixok ? Mix382off : Mix618off;
	}

    eq_B_PA = eq(B,PA);
	eq_A_P = eq(A,P);
	eq_A_Q = eq(A,Q);
	comboA3 = eq_A_P && eq_A_Q;

	// 2. Dithering edge
    if ( comboA3 && eq(A,AA) && noteq(A,PA) && noteq(A,QA) )  {	
		if (Eisblack) return theEXIT;

        // Strong blend for gradient edges
		if ( !eq_B_PA && eq(PA,QA) ) return mixok ? Mix382off : Mix618off;
        // Remaining perfect cross = dithering edge, weak blend
        // Default weak blend (no gauge border override)
		return mixok ? Mix618off : Mix854off;
	}

	// Early return if E fully transparent
	if (E==0u) return colX;

    eq_D_QA = eq(D,QA);
    eq_D_QG = eq(D,QG);
    eq_B_PC = eq(B,PC);

	// No Eisblack check for next two
  // 3. Half-dithering (outline shadow), weak blend

	if ( comboE3 && comboA3 &&
		(eq_B_PC || eq_D_QG) && eq_D_QA && eq_B_PA) {
        return mixok ? Mix618off : Mix854off;
	}

    // 4. Quarter-dithering (ugly tail artifacts: SF2 Guile plane, Cadillacs and Dinosaurs select)

	if ( comboE3 && eq_A_P
		 && eq_B_PA &&eq_D_QA && eq_D_QG
		 && eq_E_H
		) return mixok ? Mix618off : Mix854off;

	if ( comboE3 && eq_A_Q
		 && eq_B_PA &&eq_D_QA && eq_B_PC
		 && eq_E_F
		) return mixok ? Mix618off : Mix854off;

    // High priority: A-side triple alignment (after dithering)
	if (comboA3) return Xoff;

    // E-side triple alignment (after comboA3)
    if (comboE3) return mixok ? Mix382off : Xoff;


    // B-D part of long slope
	eq_B_P = eq(B,P);
	eq_D_Q = eq(D,Q);
	B_wall = eq_B_C && !eq_B_P;
	B_tower = eq_B_P && !eq_B_C;
	D_tower = eq_D_Q && !eq_D_G;
	D_wall = eq_D_G && !eq_D_Q;

    int scoreE = 0; int scoreB = 0; int scoreD = 0; int scoreZ = 0;


// E/B/D Checkerboard Scoring Rules

//	E Zone
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
        // Single line
        if (eq_E_F ||eq_E_H) return theEXIT;
    }

    if ( eq_F_H ) {
		scoreE += 1;
        if ( scoreZ==0 && B_wall && (eq(F,R) || eq(G,H) || eq(F,I)) ) scoreZ = 1;
        if ( scoreZ==0 && D_wall && (eq(C,F) || eq(H,S) || eq(F,I)) ) scoreZ = 1;
    }


	#define Bn3  eq_B_P&&eq_B_C
	#define Dn3  eq_D_G&&eq_D_Q

//	B Zone
	scoreB -= int(Bn3);
	scoreB -= int(eq(C,P));
    if (scoreB < 0) scoreZ = 0;

    if (eq_B_PA) {
		scoreB -= 1;
		scoreB -= int(eq_B_P);    // Replace eq(P,PA)
	}

//        D Zone
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

    // Final addition: 0 score, no B/D penalty = long slope
    if (scoreB >= 0 && scoreD >=0) {
        if (B_wall&&D_tower) return colX;
        if (B_tower&&D_wall) return colX;
    }

    return slopeBAD;

}	// eq_E_A


/*=========================================================
                   F - H / B+ D+ Extension Rules
  ==================================================== zz */

// Different from sim block: natural wall separation for E/En4square/BD
// Rules differ from sim side

	eq_B_P = eq(B, P);
    eq_B_PC = eq(B, PC);
	eq_D_Q = eq(D, Q);
    eq_D_QG = eq(D, QG);

    // Exit if B/D isolated
    if ( !eq_B_C && !eq_B_P && !eq_B_PC && !eq_D_G && !eq_D_Q && !eq_D_QG ) return slopeBAD;

	mixok = E!=0u && mixcheck(colX,colE);

	// Exit if E is isolated high-contrast pixel
	// Tighten threshold for bright E
	float E_lumDiff = El > 0.92 ? 0.145898 : 0.381966;
	// Large difference from neighbors
    if ( !mixok && !eq_E_I && !EalphaL && distEC>0.0638587 && distEG>0.0638587&& abs(El-Fl)>E_lumDiff && abs(El-Hl)>E_lumDiff ) return slopeBAD;


    eq_A_B = eq(A, B);
	eq_A_P = eq(A, P);
	eq_A_Q = eq(A, Q);
    eq_B_PA = eq(B,PA);
    eq_D_QA = eq(D,QA);
	comboA3 = eq_A_P && eq_A_Q;
    ThickBorder = eq_A_B && (eq_A_P||eq_A_Q|| eq(A,AA)&&(eq_B_PA||eq_D_QA));

	if (ThickBorder && !Xisblack) mixok=false;

	B_slope = eq_B_PC && !eq_B_P && !eq_B_C;
	B_tower = eq_B_P && !eq_B_PC && !eq_B_C && !eq_B_PA;
	D_slope = eq_D_QG && !eq_D_Q && !eq_D_G;
	D_tower = eq_D_Q && !eq_D_QG && !eq_D_G && !eq_D_QA;

    if (!eq_A_B) {
        // B+D+ continuous extension
        // Practice 1: Clear shape on one side = loose on other
        // Practice 2: "Factory" shape: outer flat, inner no (tower allowed, wall not)
        if ( (B_slope||B_tower) && (eq_D_QG&&!eq_D_G||D_tower) ) return Xoff;
        if ( (D_slope||D_tower) && (eq_B_PC&&!eq_B_C||B_tower) ) return Xoff;

        // High priority: A-side triple alignment
		// Note: comboA3 only with !eq_A_B in this block
        if (comboA3) return Xoff;

        // 2x2 combo supplement
        if ( B_slope && eq_A_P ) return mixok ? Mix382off : Xoff;
        if ( D_slope && eq_A_Q ) return mixok ? Mix382off : Xoff;
    }

	B_wall = eq_B_C && !eq_B_PC && !eq_B_P;

    // Clear long slope (non-solid edge, strong trend!)
    // Special handling
	if ( B_wall && D_tower ) {
        if (eq_A_B ) return slopeBAD;
        if (eq_E_F ) return colX;
        return mixok ? Mix382off : Xoff;
    }

	D_wall = eq_D_G && !eq_D_QG&& !eq_D_Q;

	if ( B_tower && D_wall ) {
        if (eq_A_B ) return slopeBAD;
        if (eq_E_H ) return colX;
        return mixok ? Mix382off : Xoff;
    }

	// No E processing needed past this point
	if (E==0u) return theEXIT;
    bool sim_X_E = sim3(colX,colE);
    bool eq_G_H = eq(G, H);
    bool eq_C_F = eq(C, F);
    bool eq_H_S = eq(H, S);
    bool eq_F_R = eq(F, R);

    En3 = eq_E_F&&eq_E_H;

    // Walled 4-pixel rectangle (En4square)
	if ( En4square ) {  // Square check after previous rule
		if (sim_X_E) return mixok ? Mix382off : Xoff;
        // Exit solid L-notch (fix font/ building corners: Mega Man 7)
        if ( (eq_B_C || eq_D_G) && eq_A_B ) return theEXIT;
        // Clear 4/6-pixel rectangle (check edges)
        if ( ( eq_B_C&&!eq_G_H || eq_D_G&&!eq_C_F || !eq_G_H&&!eq_C_F&&diffEB>0.5) && (eq_H_S == eq(I, SI) && eq_F_R == eq(I, RI)) ) return theEXIT;

        return mixok ? Mix382off : Xoff;
    }

    // Walled triangle
 	if ( En3 ) {
		if (sim_X_E) return mixok ? Mix382off : Xoff;
        if (eq_H_S && eq_F_R) return theEXIT; // Dual extension (building edges)
       // Inner bend
        if (eq_B_C || eq_D_G) return mixok ? Mix382off : Xoff;
		// Direct return for thick (Z-snake flattens outer)
        if (eq_A_B) return mixok ? Mix382off : Xoff;
        // Outer bend
        if (eq_B_P || eq_D_Q) return theEXIT;

        return mixok ? Mix382off : Xoff;
        // Rules: connect inner L, not outer (Double Dragon Jimmy eyebrows)
	}

    // F - H
	// Rule: connect inner L, not outer
	if ( eq_F_H ) {
    	if (sim_X_E||EalphaL) return mixok ? Mix382off : Xoff;
		// Exit solid L-notch (prevent symmetric crush)
		if ( eq_B_C && eq_A_B && (eq_G_H||!eq_F_R) &&eq(F, I) ) return slopeBAD;
		if ( eq_D_G && eq_A_B && (eq_C_F||!eq_H_S) &&eq(F, I) ) return slopeBAD;

		// Inner bend
        if (eq_B_C && (eq_F_R||eq_G_H||eq(F, I))) return mixok ? Mix382off : Xoff;
        if (eq_D_G && (eq_C_F||eq_H_S||eq(F, I))) return mixok ? Mix382off : Xoff;
		// Break trend: E-I F-H cross
		if (eq_E_I) return slopeBAD;
        // Z-snake outer
		if (eq_B_P && eq_A_B) return mixok ? Mix382off : Xoff;
        if (eq_D_Q && eq_A_B) return mixok ? Mix382off : Xoff;
		// Outer bend (unless long L trend)
		if (eq_B_P && (eq_C_F&&eq_H_S)) return mixok ? Mix382off : Xoff;
        if (eq_D_Q && (eq_F_R&&eq_G_H)) return mixok ? Mix382off : Xoff;

        return slopeBAD;
	}


	// Cleanup last 2 long slopes (non-clear shapes)
    // Note: Different from sim2, solid corner exit
	if ( eq_B_C && eq_D_Q || eq_D_G && eq_B_P) {
        // Prioritize eq_A_B (prevents corner clipping: Eternal Secrets)
		if (eq_A_B) return theEXIT;
		return mixok ? Mix382off : Xoff;
	}

	// Final B/D extension grab (before L single lines)
        if ( (B_slope||B_tower) && (eq_D_QG&&!eq_D_G||D_tower) ) return Xoff;
        if ( (D_slope||D_tower) && (eq_B_PC&&!eq_B_C||B_tower) ) return Xoff;


    // Exit: L-notch clear inner line (no sim_X_E pardon)
    if (eq_A_B && !ThickBorder && !eq_E_I ) {

	    if (B_wall && eq_E_F) return theEXIT;
	    if (D_wall && eq_E_H) return theEXIT;
	}

if (sim_X_E||EalphaL) return mixok ? Mix382off : Xoff;
    // Exit: L-notch hollow outer line (corner connect inner only)
	// Practice: Prevents font clipping (Captain Commando)
	if ( (B_tower || D_tower) && (eq_E_F||eq_E_H) && !eq_A_B && !eq_E_I) return theEXIT;

    // Final B/D single extension check

    // Max slope distance
    if ( B_slope && !eq_A_B && eq(PC,CC) && noteq(PC,RC)) return mixok ? Mix382off : Xoff;
    if ( D_slope && !eq_A_B && eq(QG,GG) && noteq(QG,SG)) return mixok ? Mix382off : Xoff;

    // Looser slope check if X~E
  	if ( mixok && !eq_A_B ) {
        if ( B_slope && (!eq_C_F||eq(F,RC)) ) return Mix382off;
        if ( D_slope && (!eq_G_H||eq(H,SG)) ) return Mix382off;
    }

    // Exclude single lines before Z-snake
    if ((eq_E_F||eq_E_H) && !eq_E_I ) return theEXIT;

    // Z-snake (thick eq_A_B, tower/wall on one side, no tower/wall on other)
    if ( eq_B_P && !eq_B_PA && !eq_D_Q && eq_A_B) return mixok ? Mix382off : Xoff;
    if ( eq_D_Q && !eq_D_QA && !eq_B_P && eq_A_B) return mixok ? Mix382off : Xoff;

	return theEXIT;

}	// admixX

vec4 admixS(uint A, uint B, uint C, uint D, uint E, uint F, uint G, uint H, uint I, uint R, uint RC, uint RI, uint S, uint SG, uint SI, uint II, bool eq_B_D, bool eq_E_D, float El, float Bl, vec4 colE, vec4 colF) {

    if (any_eq2(F,C,I)) return colE;
    //if (any_eq3(F,A,C,I)) return colE;

    if (eq(R, RI) && noteq(R,I)) return colE;
    if (eq(H, S) && noteq(H,I)) return colE;

    if ( eq(R, RC) || eq(G,SG) ) return colE;

	// Remove alpha channel weighting
	Bl = fract(Bl) *10.0;
	El = fract(El) *10.0;

    if ( ( eq_B_D&&eq(B,C)&&diffEB<0.381966 || eq_E_D&&vi_sim2(colE,E,C) ) &&
    (any_eq3(I,H,S,RI) || eq(SI,RI)&&noteq(I,II)) ) return colF;

    return colE;
}

void main()
{
	// Get actual pixel dimensions of texture
    vec2 source_size = vec2(textureSize(tex, 0));
    // Precompute reciprocal for performance
    vec2 inv_source_size = 1.0 / source_size;
	// Calculate sub-pixel offset within pixel grid

    vec2 pos = fract(tex_coord * source_size);
	// Force sampling coordinate to pixel geometric center

    vec2 coord = tex_coord - (pos - vec2(0.5)) * inv_source_size;


	// Map UV to float pixel coordinates
    vec2 pixelPos = tex_coord * source_size;
	// Integer index of current pixel (0-based)
	// Use floor (not round) for consistent coordinate system
    ivec2 currPixel = ivec2(floor(pixelPos)); 
	// Convert texture size to integer vector
    ivec2 texSize = ivec2(source_size);

    // Boundary check before texture sample; return transparent if out of bounds
	#define checkp(c, d) (currPixel.x+int(c)>=0 && currPixel.x+int(c)<texSize.x && currPixel.y+int(d)>=0 && currPixel.y+int(d)<texSize.y)
    #define srcf(x, y) (checkp(x, y) ? texture(tex,coord + vec2(x, y) * inv_source_size) : vec4(0.0))
    #define src(x, y) (packUnorm4x8(srcf(x,y)))

////////////////////////////////////////////////////////////////////////////////
	vec4 colE = texture(tex, coord);

	vec4 colB = srcf(0.0, -1.0);
	vec4 colD = srcf(-1.0, 0.0);
	vec4 colF = srcf(1.0, 0.0);
	vec4 colH = srcf(0.0, 1.0);

	// Default: return original pixel
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


// Skip horizontal/vertical 3x1 lines
if ( eq_E_D && eq_E_F || eq_E_B && eq_E_H) return;

    bool eq_B_H = eq(B,H);
    bool eq_D_F = eq(D,F);
// Skip symmetric block surround center
if ( eq_B_H && eq_D_F ) return;


// Sample 5x5 grid

	vec4 colA = srcf(-1.0, -1.0);
	vec4 colC = srcf(1.0, -1.0);
	vec4 colG = srcf(-1.0, 1.0);
	vec4 colI = srcf(1.0, 1.0);
	
    uint A = packUnorm4x8(colA);
    uint C = packUnorm4x8(colC);
    uint G = packUnorm4x8(colG);
    uint I = packUnorm4x8(colI);

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

// Default nearest-neighbor upscale
    vec4 J = colE;    vec4 K = colE;    vec4 L = colE;    vec4 M = colE;

// Precompute luminance
    float Bl = luma(colB);
    float Dl = luma(colD);
    float El = luma(colE);
    float Fl = luma(colF);
    float Hl = luma(colH);


// 	Pre-cal
    bool eq_B_D = eq(B,D);
    bool eq_B_F = eq(B,F);
    bool eq_D_H = eq(D,H);
    bool eq_F_H = eq(F,H);
	bool eq_E_A = eq(E, A);
	bool eq_E_C = eq(E, C);
	bool eq_E_G = eq(E, G);
	bool eq_E_I = eq(E, I);

    // Any symmetric surround
    bool oppoPix =  eq_B_H || eq_D_F;
	// Flag: entered admixX via 1:1 slope rule
    bool slope1 = false;    bool slope2 = false;    bool slope3 = false;    bool slope4 = false;
	// Standard pixel returned successfully from 1:1 slope
    bool slope1ok = false;  bool slope2ok = false;  bool slope3ok = false;  bool slope4ok = false;
	// slopeBAD: entered admixX but returned E (at least one J/K/L/M)
    // slopOFF: returned with OFF flag, skip long slope calculation

    // B - D
	if ( (!eq_E_B&&!eq_E_D&&!oppoPix) && (!eq_D_H&&!eq_B_F) && (B!=0u&&D!=0u) &&
		(eq_E_A || El>=Dl&&El>=Bl) && ( (El<Dl&&El<Bl) || none_eq2(A,B,D) || noteq(E,P) || noteq(E,Q) ) && 
		( eq_B_D&&(eq_F_H||eq_E_A||eq(B,PC)||eq(D,QG))  ||  (eq_B_D||sim1(colB,colD))&&(eq_E_C||eq_E_G||sim2(colE,colC)||sim2(colE,colG)) ) ) {
		J=admixX(A,B,C,D,E,F,G,H,I,
				P,PA,PC,Q,QA,QG,R,RC,RI,S,SG,SI,AA,CC,GG,
				El, Bl, Dl, Fl, Hl,
				colE, colB, colD, colC, colG,
				eq_B_D, eq_F_H, eq_E_A, eq_E_C, eq_E_G, eq_E_I, eq_E_F, eq_E_H);
		slope1 = true;
		if (J.b > 1.0 ) {
            if (J.b > 7.0 ) J=J-8.0; 	//slopOFF
			if (J.b == 4.0 ) return;
			if (J.b == 2.0 ) J=colE;		//slopeBAD
		} else slope1ok = true;
	}
    // B - F
	if ( !slope1 && 
		(!eq_E_B&&!eq_E_F&&!oppoPix) && (!eq_B_D&&!eq_F_H) && (B!=0u&&F!=0u) && 
		(eq_E_C || El>=Bl&&El>=Fl) && ( (El<Bl&&El<Fl) || none_eq2(C,B,F) || noteq(E,P) || noteq(E,R) ) && 
		( eq_B_F&&(eq_D_H||eq_E_C||eq(B,PA)||eq(F,RI)) ||  ( eq_B_F||sim1(colB,colF))&&(eq_E_A||eq_E_I||sim2(colE,colA)||sim2(colE,colI)) ) ) {
		K=admixX(C,F,I,B,E,H,A,D,G,
				R,RC,RI,P,PC,PA,S,SI,SG,Q,QA,QG,CC,II,AA,
				El,Fl,Bl,Hl,Dl,
				colE,colF,colB,colI,colA,
				eq_B_F, eq_D_H, eq_E_C, eq_E_I, eq_E_A, eq_E_G, eq_E_H, eq_E_D);
		slope2 = true;
		if (K.b > 1.0 ) {
            if (K.b > 7.0 ) K=K-8.0;
			if (K.b == 4.0 ) return;
			if (K.b == 2.0 ) K=colE;
		} else {slope2ok = true;}
	}
    // D - H
	if ( !slope1 && 
		(!eq_E_D&&!eq_E_H&&!oppoPix) && (!eq_F_H&&!eq_B_D) && (D!=0u&&H!=0u) && 
		(eq_E_G || El>=Hl&&El>=Dl)  &&  ((El<Hl&&El<Dl) || none_eq2(G,D,H) || noteq(E,S) || noteq(E,Q))  &&  
		( eq_D_H&&(eq_B_F||eq_E_G||eq(D,QA)||eq(H,SI)) ||  ( eq_D_H||sim1(colD,colH))&&(eq_E_A||eq_E_I||sim2(colE,colA)||sim2(colE,colI)) ) ) {
		L=admixX(G,D,A,H,E,B,I,F,C,
				Q,QG,QA,S,SG,SI,P,PA,PC,R,RI,RC,GG,AA,II,
				El,Dl,Hl,Bl,Fl,
				colE,colD,colH,colA,colI,
				eq_D_H, eq_B_F, eq_E_G, eq_E_A, eq_E_I, eq_E_C, eq_E_B, eq_E_F);
		slope3 = true;
		if (L.b > 1.0 ) {
            if (L.b > 7.0 ) L=L-8.0;
			if (L.b == 4.0 ) return;
			if (L.b == 2.0 ) L=colE;
		} else {slope3ok = true;}
	}
    // F - H
	if ( !slope2 && !slope3 && 
		(!eq_E_F&&!eq_E_H&&!oppoPix) && (!eq_B_F&&!eq_D_H) && (F!=0u&&H!=0u) && 
		(eq_E_I || El>=Fl&&El>=Hl)  &&  ((El<Fl&&El<Hl) || none_eq2(I,F,H) || noteq(E,R) || noteq(E,S))  &&  
		( eq_F_H&&(eq_B_D||eq_E_I||eq(F,RC)||eq(H,SG)) ||  ( eq_F_H||sim1(colF,colH))&&(eq_E_C||eq_E_G||sim2(colE,colC)||sim2(colE,colG)) ) ) {
		M=admixX(I,H,G,F,E,D,C,B,A,
				S,SI,SG,R,RI,RC,Q,QG,QA,P,PC,PA,II,GG,CC,
				El,Hl,Fl,Dl,Bl,
				colE,colH,colF,colG,colC,
				eq_F_H, eq_B_D, eq_E_I, eq_E_G, eq_E_C, eq_E_A, eq_E_D, eq_E_B);
		slope4 = true;
		if (M.b > 1.0 ) {
            if (M.b > 7.0 ) M=M-8.0;
			if (M.b == 4.0 ) return;
			if (M.b == 2.0 ) M=colE;
		} else {slope4ok = true;}
	}


//  Long gentle 2:1 slope  (P100)

	bool longslope = false;

    if (slope4ok && eq_F_H) { //zone4 long slope
        // Original ext 1: Pass sample to admixL to prevent double-blend
        // Original ext 2: No L-shape in gap unless walled
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

// Exit after long slope (no diagonal sawslope)
bool skiprest = longslope;

bool slopeok = slope1ok||slope2ok||slope3ok||slope4ok;


// Note: sawslope excludes slopeok (strong shapes), includes slopeOFF/BAD
if (!skiprest && !oppoPix && !slopeok) {


        // horizontal bottom
		if (!eq_E_H && none_eq2(H,A,C)) {

			//                                    A B C
			//                                  Q D E F R       Zone 4
			//					                G H I
			//					                  S
			if ( (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H)  && !eq_F_H && F!=0u &&
                !eq_E_F && (eq_B_D || eq_E_D) && eq(R,H) && eq(F,G) ) {
                M = admixS(A,B,C,D,E,F,G,H,I,R,RC,RI,S,SG,SI,II,eq_B_D,eq_E_D,El,Bl,colE,colF);
                skiprest = true;}

			//                                  . A B C
			//                                  Q D E F R       Zone 3
			//                                     G H I
			//					                   S
			if ( !skiprest && (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && !eq_D_H && D!=0u &&
                 !eq_E_D && (eq_B_F || eq_E_F) && eq(Q,H) && eq(D,I) ) {
                L = admixS(C,B,A,F,E,D,I,H,G,Q,QA,QG,S,SI,SG,GG,eq_B_F,eq_E_F,El,Bl,colE,colD);
                skiprest = true;}
		}

        // horizontal up
		if ( !skiprest && !eq_E_B && none_eq2(B,G,I)) {

			//					                   P
			//                                    A B C
			//                                  Q D E F R       Zone 2
			//                                    G H I .
			if ( (!slope1 && !eq_B_D)  && (!slope4 && !eq_F_H) && !eq_B_F && F!=0u &&
				  !eq_E_F && (eq_D_H || eq_E_D) && eq(B,R) && eq(A,F) ) {
                K = admixS(G,H,I,D,E,F,A,B,C,R,RI,RC,P,PA,PC,CC,eq_D_H,eq_E_D,El,Hl,colE,colF);
                skiprest = true;}

			//					                  P
			//                                    A B C
			//                                 Q D E F R        Zone 1
			//                                  . G H I
			if ( !skiprest && (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H) && !eq_B_D && D!=0u &&
				 !eq_E_D && (eq_F_H || eq_E_F) && eq(B,Q) && eq(C,D) ) {
                J = admixS(I,H,G,F,E,D,C,B,A,Q,QG,QA,P,PC,PA,AA,eq_F_H,eq_E_F,El,Hl,colE,colD);
                skiprest = true;}

		}

        // vertical left
        if ( !skiprest && !eq_E_D && none_eq2(D,C,I) ) {

			//                                    A B C
			//                                  Q D E F R
			//                                    G H I        Zone 3
			//                                       S .
            if ( (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && !eq_D_H && H!=0u &&
				  !eq_E_H && (eq_B_F || eq_E_B) && eq(D,S) && eq(A,H) ) {
                L = admixS(C,F,I,B,E,H,A,D,G,S,SI,SG,Q,QA,QG,GG,eq_B_F,eq_E_B,El,Fl,colE,colH);
                skiprest = true;}

			//                                      P .
			//                                    A B C
			//                                  Q D E F R       Zone 1
			//                                    G H I
			if ( !skiprest && (!slope3 && !eq_D_H) && (!slope2 && !eq_B_F) && !eq_B_D && B!=0u &&
				  !eq_E_B && (eq_F_H || eq_E_H) && eq(P,D) && eq(B,G) ) {
                J = admixS(I,F,C,H,E,B,G,D,A,P,PC,PA,Q,QG,QA,AA,eq_F_H,eq_E_H,El,Fl,colE,colB);
                skiprest = true;}

		}

        // vertical right
		if ( !skiprest && !eq_E_F && none_eq2(F,A,G) ) { // right

			//                                    A B C
			//                                  Q D E F R
			//                                    G H I        Zone 4
			//                                    . S
			if ( (!slope2 && !eq_B_F) && (!slope3 && !eq_D_H) && !eq_F_H && H!=0u &&
				  !eq_E_H && (eq_B_D || eq_E_B) && eq(S,F) && eq(H,C) ) {
                M = admixS(A,D,G,B,E,H,C,F,I,S,SG,SI,R,RC,RI,II,eq_B_D,eq_E_B,El,Dl,colE,colH);
                skiprest = true;}

			//                                    . P
			//                                    A B C
			//                                  Q D E F R        Zone 2
			//                                    G H I
			if ( !skiprest && (!slope1 && !eq_B_D) && (!slope4 && !eq_F_H) && !eq_B_F && B!=0u &&
				 !eq_E_B && (eq_D_H || eq_E_H) && eq(P,F) && eq(B,I) ) {
                K = admixS(G,D,A,H,E,B,I,F,C,P,PA,PC,R,RI,RC,CC,eq_D_H,eq_E_H,El,Dl,colE,colB);
                skiprest = true;}

		} // vertical right
} // sawslope

// Exit after sawslope; old: skiprest||slopeBAD (uses slopeOFF/BAD weakly)
skiprest = skiprest||slope1||slope2||slope3||slope4||E==0u||B==0u||D==0u||F==0u||H==0u;

/**************************************************
       Concave + Cross Pattern	（P100）	   
 *************************************************/
// Use approximate pixels for cross distance; useful for lines/aliasing/gradients
// e.g. SF2 III intro glow text, SFZ3 mix Japanese houses, WoF intro

	vec4 colX;		// Temp X

    if (!skiprest &&
        Bl<El && !eq_E_D && !eq_E_F && eq_E_H && !eq_E_A && !eq_E_C && all_eq2(G,H,I) && vi_sim2(colE,E,S) ) { // TOP

        if (eq_B_D||eq_B_F) { J=admixK(colB,J);    K=J;
            if (eq_D_F) { L=mix(J,L, 0.61804);   M=L; }
        } else { colX = El-Bl < abs(El-Dl) ? colB : colD;  J=admixC(colX,J);
			if (eq_D_F) { K=J;  L=mix(J,L, 0.61804);    M=L; }
			else {colX = El-Bl < abs(El-Fl) ? colB : colF; 		K=admixC(colX,K); }
            }

	   skiprest = true;
	}

    if (!skiprest &&
		Hl<El && !eq_E_D && !eq_E_F && eq_E_B && !eq_E_G && !eq_E_I && all_eq2(A,B,C) && vi_sim2(colE,E,P) ) { // BOTTOM

        if (eq_D_H||eq_F_H) { L=admixK(colH,L);    M=L;
            if (eq_D_F) { J=mix(L,J, 0.61804);   K=J; }
        } else { colX = El-Hl < abs(El-Dl) ? colH : colD;  L=admixC(colX,L);
			if (eq_D_F) { M=L;  J=mix(L,J, 0.61804);    K=J; }
			else { colX = El-Hl < abs(El-Fl) ? colH : colF;    M=admixC(colX,M); }
            }

	   skiprest = true;
	}

   if (!skiprest &&
		Fl<El && !eq_E_B && !eq_E_H && eq_E_D && !eq_E_C && !eq_E_I && all_eq2(A,D,G) && vi_sim2(colE,E,Q) ) { // RIGHT

        if (eq_B_F||eq_F_H) { K=admixK(colF,K);    M=K;
            if (eq_B_H) { J=mix(K,J, 0.61804);   L=J; }
        } else { colX = El-Fl < abs(El-Bl) ? colF : colB;  K=admixC(colX,K);
			if (eq_B_H) { M=K;  J=mix(K,J, 0.61804);    L=J; }
			else { colX = El-Fl < abs(El-Hl) ? colF : colH;    M=admixC(colX,M); }
            }

	   skiprest = true;
	}

    if (!skiprest &&
		Dl<El && !eq_E_B && !eq_E_H && eq_E_F && !eq_E_A && !eq_E_G && all_eq2(C,F,I) && vi_sim2(colE,E,R) ) { // LEFT

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
   	    ✕О    Scorpion Pattern (P99). Resembles Matrix sentinel. Fixes regular staggered pixels.
*/

   if (!skiprest && !eq_E_F &&eq_E_D&&eq_B_F&&eq_F_H && eq_E_C && eq_E_I && noteq(F,src(+3.0, 0.0)) ) {K=admixK(colF,K); M=K;J=mix(K,J, 0.61804); L=J;skiprest=true;}	// RIGHT
   if (!skiprest && !eq_E_D &&eq_E_F&&eq_B_D&&eq_D_H && eq_E_A && eq_E_G && noteq(D,src(-3.0, 0.0)) ) {J=admixK(colD,J); L=J;K=mix(J,K, 0.61804); M=K;skiprest=true;}	// LEFT
   if (!skiprest && !eq_E_H &&eq_E_B&&eq_D_H&&eq_F_H && eq_E_G && eq_E_I && noteq(H,src(0.0, +3.0)) ) {L=admixK(colH,L); M=L;J=mix(L,J, 0.61804); K=J;skiprest=true;}	// BOTTOM
   if (!skiprest && !eq_E_B &&eq_E_H&&eq_B_D&&eq_B_F && eq_E_A && eq_E_C && noteq(B,src(0.0, -3.0)) ) {J=admixK(colB,J); K=J;L=mix(J,L, 0.61804); M=L;}				// TOP

	//final write
    frag_color = (pos.x < 0.5) ? (pos.y < 0.5 ? J : L) : (pos.y < 0.5 ? K : M);
}