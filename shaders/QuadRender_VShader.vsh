#define aCommand v0
#pragma bind_symbol(aCommand, v0,v0)
#define aParameters v1
#pragma bind_symbol(aParameters, v1,v1)

#pragma output_map (generic, o0)
#pragma output_map (generic, o1)

main:
	//just pass these along to the geometry shader
	mov r0, aCommand
	mov r1, aParameters
	mov o0, r0
	mov o1, r1
    end
endmain:
