

#include "gles_system.h"
#include "tarray.h"
#include "v_video.h"
#include "printf.h"

CVAR(Bool, gles_use_mapped_buffer, false, 0);
CVAR(Bool, gles_force_glsl_v100, false, 0);
CVAR(Int, gles_max_lights_per_surface, 32, 0);
EXTERN_CVAR(Bool, gl_customshader);


#if USE_GLES2 && !defined(__3DS__)

PFNGLMAPBUFFERRANGEEXTPROC glMapBufferRange = NULL;
PFNGLUNMAPBUFFEROESPROC glUnmapBuffer = NULL;

#ifdef __ANDROID__
#include <dlfcn.h>

static void* LoadGLES2Proc(const char* name)
{
	static void *glesLib = NULL;

	if(!glesLib)
	{
		int flags = RTLD_LOCAL | RTLD_NOW;

		glesLib = dlopen("libGLESv2_CM.so", flags);
		if(!glesLib)
		{
			glesLib = dlopen("libGLESv2.so", flags);
		}
		if(!glesLib)
		{
			glesLib = dlopen("libGLESv2.so.2", flags);
		}
	}

	void * ret = NULL;
	ret =  dlsym(glesLib, name);

	if(!ret)
	{
		//LOGI("Failed to load: %s", name);
	}
	else
	{
		//LOGI("Loaded %s func OK", name);
	}

	return ret;
}

#elif defined _WIN32

#include <windows.h>

static HMODULE opengl32dll;
static PROC(WINAPI* getprocaddress)(LPCSTR name);

static void* LoadGLES2Proc(const char* name)
{

	HINSTANCE hGetProcIDDLL = LoadLibraryA("libGLESv2.dll");
	
	int error =	GetLastError();

	void* addr = GetProcAddress(hGetProcIDDLL, name);
	if (!addr)
	{
		//exit(1);
	}
	else
	{
		return addr;
	}
}

#endif

#endif // USE_GLES2

static TArray<FString>  m_Extensions;


static void CollectExtensions()
{
	const char* supported = (char*)glGetString(GL_EXTENSIONS);

	if (nullptr != supported)
	{
		char* extensions = new char[strlen(supported) + 1];
		strcpy(extensions, supported);

		char* extension = strtok(extensions, " ");

		while (extension)
		{
			m_Extensions.Push(FString(extension));
			extension = strtok(nullptr, " ");
		}

		delete[] extensions;
	}
}


static bool CheckExtension(const char* ext)
{
	for (unsigned int i = 0; i < m_Extensions.Size(); ++i)
	{
		if (m_Extensions[i].CompareNoCase(ext) == 0) return true;
	}

	return false;
}

namespace OpenGLESRenderer
{
	RenderContextGLES gles;

	void InitGLES()
	{

#if USE_GLES2

#if !defined(__3DS__)
		if (!gladLoadGLES2Loader(&LoadGLES2Proc))
		{
			exit(-1);
		}

		glMapBufferRange = (PFNGLMAPBUFFERRANGEEXTPROC)LoadGLES2Proc("glMapBufferRange");
		glUnmapBuffer = (PFNGLUNMAPBUFFEROESPROC)LoadGLES2Proc("glUnmapBuffer");
#endif

#else
		static bool first = true;

		if (first)
		{
			if (ogl_LoadFunctions() == ogl_LOAD_FAILED)
			{
				//I_FatalError("Failed to load OpenGL functions.");
			}
		}
		GLuint vao;
		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);
#endif
		CollectExtensions();

		Printf("GL_VENDOR: %s\n", glGetString(GL_VENDOR));
		Printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
		Printf("GL_VERSION: %s\n", glGetString(GL_VERSION));
		Printf("GL_SHADING_LANGUAGE_VERSION: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
		Printf(PRINT_LOG, "GL_EXTENSIONS:\n");
		for (unsigned i = 0; i < m_Extensions.Size(); i++)
		{
			Printf(" %s\n", m_Extensions[i].GetChars());
		}


		gles.flags = RFL_NO_CLIP_PLANES;

#ifdef __3DS__
		// NovaGL cannot honor GL_MAP_UNSYNCHRONIZED_BIT with a PICA completion
		// fence. Direct mapping therefore lets the CPU rewrite world vertices that
		// queued draws are still fetching: MAP01 can be correct for one frame and
		// fold into corrupted geometry immediately afterwards. New 3DS has room
		// for the 2 MiB CPU shadow, so upload only completed ranges instead.
		gles.useMappedBuffers = false;
		gles.forceGLSLv100 = true;
		gles.maxlights = 0;
		Printf("[3DS] VBO mode: shadowed range uploads (direct mapping disabled)\n");
#else
		gles.useMappedBuffers = gles_use_mapped_buffer;
		gles.forceGLSLv100 = gles_force_glsl_v100;
		gles.maxlights = gles_max_lights_per_surface;
#endif

		gles.modelstring = (char*)glGetString(GL_RENDERER);
		gles.vendorstring = (char*)glGetString(GL_VENDOR);
	
		gl_customshader = false;

		GLint maxTextureSize[1];
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, maxTextureSize);

		gles.max_texturesize = maxTextureSize[0];
#ifdef __3DS__
		// At 400x240, 256px preserves the pixel-art source while placing a hard
		// 256 KiB ceiling on one RGBA texture. Legend of Doom ships several
		// 1K-2K menu/map images; allowing their POT-expanded desktop sizes would
		// consume the entire PICA/linear texture budget for pixels never shown.
		if (gles.max_texturesize > 256) gles.max_texturesize = 256;
#endif
		
		Printf("GL_MAX_TEXTURE_SIZE: %d\n", gles.max_texturesize);

#if USE_GLES2
#ifdef __3DS__
		gles.depthStencilAvailable = true;
		gles.npotAvailable = true;
#else
		gles.depthStencilAvailable = CheckExtension("GL_OES_packed_depth_stencil");
		gles.npotAvailable = CheckExtension("GL_OES_texture_npot");
#endif
#else
		gles.depthStencilAvailable = true;
		gles.npotAvailable = true;
		gles.useMappedBuffers = true;
#endif
		
		gles.numlightvectors = (gles.maxlights * LIGHT_VEC4_NUM);
	}
}
