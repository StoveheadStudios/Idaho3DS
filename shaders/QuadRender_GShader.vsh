#pragma gs_assemble_as_geometry_shader
#pragma gs_data_mode ( 0 ) //normal mode - one invocation of geometry shader per vertex

//==============================================
//boilerplate defines

//------------------------------------------
//  related cmp
//------------------------------------------
#define CMP_MODE_EQ         0
#define CMP_MODE_NE         1
#define CMP_MODE_LT         2
#define CMP_MODE_LE         3
#define CMP_MODE_GT         4
#define CMP_MODE_GE         5

//------------------------------------------
//  related condition
//------------------------------------------
#define COND_MODE_OR        0
#define COND_MODE_AND       1
#define COND_MODE_STA0      2
#define COND_MODE_STA1      3

//------------------------------------------
//  related status reg
//------------------------------------------
#define STAT0_0             0
#define STAT0_1             1
#define STAT1_0             0
#define STAT1_1             1

#define bInitialized    b15
//#pragma bind_symbol(ubInitialized,b15,b15) //dont think i need this..

//==============================================

#define aCommand v0
#pragma bind_symbol(aCommand.x, v0,v0)
#define aParameters v1
#pragma bind_symbol(aParameters.xyzw, v1,v1)

#define oPosition o0
#pragma output_map(position, o0)
#define oTexcoord0 o1
#pragma output_map(texture0, o1)
#define oPrimaryColor o2
#pragma output_map (color, o2)
#define oTexcoord1 o3
#pragma output_map(texture1, o3)
#define oQuaternion o4
#pragma output_map (quaternion, o4)
#define oView o5
#pragma output_map (view, o5)

#define uProjection c0
#pragma bind_symbol(uProjection,c0,c3)
#define uExtraTexcoords c4
#pragma bind_symbol(uExtraTexcoords,c4,c5)

#define rTemp r0
#define rTemp2 r1
#define rPrimaryColor r2
#define rTexParams r3
#define rCorners r4
#define rMisc r5
	#define rStereoAdjust_y r5.y
	#define rStereoAdjust_x r5.x
#define rFreeQuadExtra r6
//r7 is free
#define rModelviewMatrix r8
#define rMODELVIEWPROJ r12

def c16, 0,1,2,3
#define c_0 c16.x
#define c_1 c16.y
#define c_0_1 c16.xy
#define c_2_3 c16.zw

def c17, 4,5,6,7
#define c_4_5 c17.xy
#define c_6_7 c17.zw

def c18, 0,0,0,1
#define c_0_0_0_1 c18

def c19, 0, 0, 0, 1 //basically means 'dont rotate the surface/bump normal'. corresponds to our view, with a tangent on +x and binormal on +y
def c20, 0, 0, 1, -1 //view vector.. opposite normal vector, right? I don't know. It works.

//I wonder if I could make this faster by pulling apart the command byte into two fields and then using one of them to index an array of registers
//so it'd be like switch(cmd&0x0F) { case 0x01: regs[cmd>>8] = param }
main:

	//identify opcode 0x00 and 0x01
	cmp CMP_MODE_EQ, CMP_MODE_EQ, aCommand.xx, c_0_1
	jpc 1, 1, COND_MODE_STA0, OPCODE_quad
	jpc 1, 1, COND_MODE_STA1, OPCODE_texparams

	//identify opcode 0x02 and 0x03 (primary color and misc)
	cmp CMP_MODE_EQ, CMP_MODE_EQ, aCommand.xx, c_2_3
	jpc 1, 1, COND_MODE_STA0, OPCODE_primaryColor
	jpc 1, 1, COND_MODE_STA1, OPCODE_misc

	//identify opcode 0x04 and 0x05 (modelview rows)
	cmp CMP_MODE_EQ, CMP_MODE_EQ, aCommand.xx, c_4_5
	jpc 1, 1, COND_MODE_STA0, OPCODE_mv0
	jpc 1, 1, COND_MODE_STA1, OPCODE_mv1

	cmp CMP_MODE_EQ, CMP_MODE_EQ, aCommand.xx, c_6_7
	jpc 1, 1, COND_MODE_STA0, OPCODE_mv2
	jpc 1, 1, COND_MODE_STA1, OPCODE_tri

	//unknown opcode.. just ignore it
	//BUT.. for now.. lets make it...
	//special for EmitFreeQuad!
	mov rFreeQuadExtra, aParameters
	end

OPCODE_quad:
	call concat_m
		
	//aParameters: xdest, ydest, width, height

	//setup corner registers and initialize a temp position register with z=0,w=1
	mov rCorners, aParameters
	mov rTemp.zw, c_0_1
	add rCorners.zw, aParameters.xxxy, aParameters.xxzw //rCorners.zw are now the bottom right

	//topleft
	mov rTemp.xy, rCorners.xy
	mov oTexcoord0, rTexParams.xy
	mov oTexcoord1, uExtraTexcoords[0].xy
	m4x4 rTemp2, rTemp, rMODELVIEWPROJ
	mov oPosition, rTemp2
	mov oPrimaryColor, rPrimaryColor //output bank 1 of 2 color assignment
	mov oQuaternion, c19 //THIS MAY BE NEEDED. OR IT MAY NOT BE.
	mov oView, c20 //THIS MAY BE NEEDED. OR IT MAY NOT BE.
	smf 0, 0, 0 //submit vertex 0 (top left)
	vout

	//bottomright
	mov rTemp.xy, rCorners.zw
	mov oTexcoord0, rTexParams.zw //uv = u1,v1
	mov oTexcoord1, uExtraTexcoords[1].zw
	m4x4 rTemp2, rTemp, rMODELVIEWPROJ
	mov oPosition, rTemp2
	mov oPrimaryColor, rPrimaryColor //output bank 2 of 2 color assignment (and now it doesnt need to be set anymore)
	mov oQuaternion, c19 //THIS MAY BE NEEDED. OR IT MAY NOT BE.
	mov oView, c20 //THIS MAY BE NEEDED. OR IT MAY NOT BE.
	smf 0, 0, 1 //submit vertex 1 (bottom right)
	vout

	//topright
	mov rTemp.xy, rCorners.zy
	mov oTexcoord0, rTexParams.zy //uv = u1,v0
	mov oTexcoord1, uExtraTexcoords[0].zw
	m4x4 rTemp2, rTemp, rMODELVIEWPROJ
	mov oPosition, rTemp2
	//oQuaternion and oView are set from the previous two verts
	smf 1, 1, 2 //submit vertex 2 (top right) and complete counterclockwise triangle
	vout

	//bottomleft
	mov rTemp.xy, rCorners.xw
	mov oTexcoord0, rTexParams.xw //uv = u0,v1
	mov oTexcoord1, uExtraTexcoords[1].xy
	m4x4 rTemp2, rTemp, rMODELVIEWPROJ
	mov oPosition, rTemp2
	//oQuaternion and oView are set from the previous two verts
	smf 0, 1, 2 //submit vertex 2 (bottom left) and complete clockwise triangle
	vout

	end

OPCODE_texparams:
	mov rTexParams, aParameters
	end

OPCODE_primaryColor:
	mov rPrimaryColor, aParameters
	end

OPCODE_misc:
	mov rMisc, aParameters
	end

OPCODE_mv0:
	mov rModelviewMatrix[0], aParameters
	end
OPCODE_mv1:
	mov rModelviewMatrix[1], aParameters
	end
OPCODE_mv2:
	mov rModelviewMatrix[2], aParameters
	end
OPCODE_tri:
	//rTexParams (prior): x0,y0,u0,v0
	//rMisc (prior): x1,y1,u1,v1
	//aParameters: x2,y2,u2,v2
	
	call concat_m

	mov rTemp.zw, c_0_1

	//vert 0
	mov rTemp.xy, rTexParams.xy
	mov oTexcoord0, rTexParams.zw
	mov oTexcoord1, uExtraTexcoords[0].xy
	m4x4 rTemp2, rTemp, rMODELVIEWPROJ
	mov oPosition, rTemp2
	mov oPrimaryColor, rPrimaryColor //output bank 1 of 2 color assignment
	//TODO: oQuaternion and oView, as needed
	smf 0, 0, 0 //submit vertex 0
	vout

	//vert 1
	mov rTemp.xy, rFreeQuadExtra.xy
	mov oTexcoord0, rFreeQuadExtra.zw
	mov oTexcoord1, uExtraTexcoords[0].xy
	m4x4 rTemp2, rTemp, rMODELVIEWPROJ
	mov oPosition, rTemp2
	mov oPrimaryColor, rPrimaryColor //output bank 2 of 2 color assignment (and now it doesnt need to be set anymore)
	//TODO: oQuaternion and oView, as needed
	smf 0, 0, 1 //submit vertex 1 (bottom right)
	vout

	//vert 2
	mov rTemp.xy, aParameters.xy
	mov oTexcoord0, aParameters.zw
	mov oTexcoord1, uExtraTexcoords[0].xy
	m4x4 rTemp2, rTemp, rMODELVIEWPROJ
	mov oPosition, rTemp2
	smf 1, 1, 2 //submit vertex 2 (top right) and complete counterclockwise triangle
	vout


	end
	
end
endmain:


concat_m:
	//concatenate MV * P
	//we have to do this somewhere, so why not here? that way we can keep normal independent modelview and projection semantics in the engine code
	#define MVP rMODELVIEWPROJ
	#define MV rModelviewMatrix
	#define P uProjection
		//TODO - could i do a transpose when i emit the modelview matrix commands? or when i process them in here?
		//TODO - could i only do this when the last row of the modelview matrix changes?
		//TODO - could i alter it each time a row changes, just doing the calculation for a row? that would save me some temp registers i believe
		//TODO - could i only upload the pieces of the MV that change? frequently it will just be the translates
			
		//pre-patch modelview matrix with stereoadjust. is this safe linear algebra? seems to work OK.
		add rModelviewMatrix[0].w, rModelviewMatrix[0].wwww, rMisc.xxxx
		add rModelviewMatrix[1].w, rModelviewMatrix[1].wwww, rMisc.yyyy

		mov rTemp, c_0_0_0_1
		mul MVP[0], rTemp, P[0].w 
		mul MVP[1], rTemp, P[1].w
		mul MVP[2], rTemp, P[2].w
		mul MVP[3], rTemp, P[3].w
		mad MVP[0], MV[2], P[0].z, MVP[0]
		mad MVP[1], MV[2], P[1].z, MVP[1]
		mad MVP[2], MV[2], P[2].z, MVP[2]
		mad MVP[3], MV[2], P[3].z, MVP[3]
		mad MVP[0], MV[1], P[0].y, MVP[0]
		mad MVP[1], MV[1], P[1].y, MVP[1]
		mad MVP[2], MV[1], P[2].y, MVP[2]
		mad MVP[3], MV[1], P[3].y, MVP[3]
		mad MVP[0], MV[0], P[0].x, MVP[0]
		mad MVP[1], MV[0], P[1].x, MVP[1]
		mad MVP[2], MV[0], P[2].x, MVP[2]
		mad MVP[3], MV[0], P[3].x, MVP[3]

		//fix back modelview matrix. uhhhh is this stable math?
		add rModelviewMatrix[1].w, rModelviewMatrix[1].w, -rStereoAdjust_y
		add rModelviewMatrix[0].w, rModelviewMatrix[0].w, -rStereoAdjust_x
	#undef MVP
	#undef MV
	#undef P
ret
