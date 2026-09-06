#ifndef PIKMIN_PC_OPENGL_H
#define PIKMIN_PC_OPENGL_H

// Punto único de entrada a las cabeceras de OpenGL.
//
// En Windows, <GL/gl.h> y <GL/glext.h> necesitan las convenciones de llamada
// APIENTRY, WINGDIAPI y CALLBACK, que normalmente obtienen incluyendo
// <windows.h>. Aquí no podemos permitirlo: windows.h define macros con nombres
// muy comunes, y una de ellas es ERROR, que este proyecto usa como macro de
// registro propia (include/DebugLog.h) en 371 archivos. Incluir windows.h en la
// ruta gráfica la aplastaría.
//
// Tanto glext.h como gl.h condicionan su #include <windows.h> a que APIENTRY no
// esté ya definida, así que definiéndola nosotros primero se saltan windows.h
// por completo y obtenemos solo OpenGL. Las definiciones son las mismas que
// windows.h habría proporcionado.
//
// El juego carga las funciones modernas de GL con SDL_GL_GetProcAddress, así
// que no hace falta ni GLAD ni GLEW en ningún sistema.

#ifdef _WIN32
#  ifndef APIENTRY
#    define APIENTRY __stdcall
#  endif
#  ifndef WINGDIAPI
#    define WINGDIAPI __declspec(dllimport)
#  endif
#  ifndef CALLBACK
#    define CALLBACK __stdcall
#  endif
#endif

#include <GL/gl.h>
#include <GL/glext.h>

// Red de seguridad: si alguna cabecera del sistema acabara arrastrando
// windows.h de todos modos, estas macros suyas chocan con identificadores del
// juego. Retirarlas aquí hace que un conflicto se manifieste como un error de
// compilación claro en vez de como comportamiento extraño.
#ifdef _WIN32
#  undef near
#  undef far
#  undef small
#endif

#endif // PIKMIN_PC_OPENGL_H
