/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include "Rendering/Shaders/Shader.h"
#include "Rendering/Shaders/GLSLCopyState.h"
#include "Rendering/GL/myGL.h"
#include "Rendering/GlobalRendering.h"
#include "System/FileSystem/FileHandler.h"
#include "System/Log/ILog.h"
#include <algorithm>


#define LOG_SECTION_SHADER "Shader"
LOG_REGISTER_SECTION_GLOBAL(LOG_SECTION_SHADER)

// use the specific section for all LOG*() calls in this source file
#ifdef LOG_SECTION_CURRENT
	#undef LOG_SECTION_CURRENT
#endif
#define LOG_SECTION_CURRENT LOG_SECTION_SHADER


static bool glslIsValid(GLuint obj)
{
	const bool isShader = glIsShader(obj);
	assert(glIsShader(obj) || glIsProgram(obj));

	GLint compiled;
	if (isShader)
		glGetShaderiv(obj, GL_COMPILE_STATUS, &compiled);
	else
		glGetProgramiv(obj, GL_LINK_STATUS, &compiled);

	return compiled;
}


static std::string glslGetLog(GLuint obj)
{
	const bool isShader = glIsShader(obj);
	assert(glIsShader(obj) || glIsProgram(obj));

	int infologLength = 0;
	int maxLength;

	if (isShader)
		glGetShaderiv(obj, GL_INFO_LOG_LENGTH, &maxLength);
	else
		glGetProgramiv(obj, GL_INFO_LOG_LENGTH, &maxLength);

	std::string infoLog;
	infoLog.resize(maxLength);

	if (isShader)
		glGetShaderInfoLog(obj, maxLength, &infologLength, &infoLog[0]);
	else
		glGetProgramInfoLog(obj, maxLength, &infologLength, &infoLog[0]);

	infoLog.resize(infologLength);
	return infoLog;
}


static std::string GetShaderSource(std::string fileName)
{
	std::string soPath = "shaders/" + fileName;
	std::string soSource = "";

	CFileHandler soFile(soPath);

	if (soFile.FileExists()) {
		soSource.resize(soFile.FileSize());
		soFile.Read(&soSource[0], soFile.FileSize());
	} else {
		LOG_L(L_ERROR, "File not found \"%s\"", soPath.c_str());
	}

	return soSource;
}




namespace Shader {
	static NullShaderObject nullShaderObject_(0,"");
	static NullProgramObject nullProgramObject_("NullProgram");
	NullShaderObject* nullShaderObject = &nullShaderObject_;
	NullProgramObject* nullProgramObject = &nullProgramObject_;


	ARBShaderObject::ARBShaderObject(int shType, const std::string& shSrc, const std::string& shDefinitions)
		: IShaderObject(shType, shSrc)
	{
		assert(globalRendering->haveARB); // non-debug check is done in ShaderHandler
		glGenProgramsARB(1, &objID);
	}

	void ARBShaderObject::Compile() {
		glEnable(type);

		const std::string srcStr = GetShaderSource(srcFile);

		glBindProgramARB(type, objID);
		glProgramStringARB(type, GL_PROGRAM_FORMAT_ASCII_ARB, srcStr.size(), srcStr.c_str());

		int errorPos = -1;
		int isNative =  0;

		glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &errorPos);
		glGetProgramivARB(type, GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB, &isNative);

		const char* err = (const char*) glGetString(GL_PROGRAM_ERROR_STRING_ARB);

		log = std::string((err != NULL)? err: "[NULL]");
		valid = (errorPos == -1 && isNative != 0);

		glBindProgramARB(type, 0);

		glDisable(type);
	}
	void ARBShaderObject::Release() {
		glDeleteProgramsARB(1, &objID);
	}



	GLSLShaderObject::GLSLShaderObject(int shType, const std::string& shSrcFile, const std::string& shDefinitions)
		: IShaderObject(shType, shSrcFile, shDefinitions)
	{
		assert(globalRendering->haveGLSL); // non-debug check is done in ShaderHandler
	}
	void GLSLShaderObject::Compile() {
		const std::string srcStr = GetShaderSource(srcFile).c_str();

		const GLchar* sources[4] = {
			"#line 0\n",
			definitions.c_str(),
			"\n#line 0\n",
			srcStr.c_str()
		};
		const GLint lengths[4] = {-1, -1, -1, -1};

		if (!objID)
			objID = glCreateShader(type);
		glShaderSource(objID, 4, sources, lengths);
		glCompileShader(objID);

		valid = glslIsValid(objID);
		log   = glslGetLog(objID);

		if (!IsValid()) {
			LOG_L(L_WARNING, "[%s]\n\tshader-object name: %s, compile-log:\n%s",
				__FUNCTION__, srcFile.c_str(), GetLog().c_str());
		}
	}

	void GLSLShaderObject::Release() {
		glDeleteShader(objID);
		objID = 0;
	}






	void IProgramObject::Release() {
		for (SOVecIt it = shaderObjs.begin(); it != shaderObjs.end(); ++it) {
			(*it)->Release();
			delete *it;
		}

		shaderObjs.clear();
	}
	bool IProgramObject::IsShaderAttached(const IShaderObject* so) const {
		return (std::find(shaderObjs.begin(), shaderObjs.end(), so) != shaderObjs.end());
	}


	ARBProgramObject::ARBProgramObject(const std::string& poName): IProgramObject(poName) {
		objID = -1; // not used for ARBProgramObject instances
		uniformTarget = -1;
	}

	void ARBProgramObject::Enable() const {
		for (SOVecConstIt it = shaderObjs.begin(); it != shaderObjs.end(); it++) {
			glEnable((*it)->GetType());
			glBindProgramARB((*it)->GetType(), (*it)->GetObjID());
		}
	}
	void ARBProgramObject::Disable() const {
		for (SOVecConstIt it = shaderObjs.begin(); it != shaderObjs.end(); it++) {
			glBindProgramARB((*it)->GetType(), 0);
			glDisable((*it)->GetType());
		}
	}

	void ARBProgramObject::Link() {
		bool shaderObjectsValid = true;

		for (SOVecConstIt it = shaderObjs.begin(); it != shaderObjs.end(); it++) {
			shaderObjectsValid = (shaderObjectsValid && (*it)->IsValid());
		}

		valid = shaderObjectsValid;
	}
	void ARBProgramObject::Release() {
		IProgramObject::Release();
	}
	void ARBProgramObject::Reload() {
		
	}

	#define glPEP4f  glProgramEnvParameter4fARB
	#define glPEP4fv glProgramEnvParameter4fvARB
	void ARBProgramObject::SetUniform1i(int idx, int   v0                              ) const { glPEP4f(uniformTarget, idx, float(v0), float( 0), float( 0), float( 0)); }
	void ARBProgramObject::SetUniform2i(int idx, int   v0, int   v1                    ) const { glPEP4f(uniformTarget, idx, float(v0), float(v1), float( 0), float( 0)); }
	void ARBProgramObject::SetUniform3i(int idx, int   v0, int   v1, int   v2          ) const { glPEP4f(uniformTarget, idx, float(v0), float(v1), float(v2), float( 0)); }
	void ARBProgramObject::SetUniform4i(int idx, int   v0, int   v1, int   v2, int   v3) const { glPEP4f(uniformTarget, idx, float(v0), float(v1), float(v2), float(v3)); }
	void ARBProgramObject::SetUniform1f(int idx, float v0                              ) const { glPEP4f(uniformTarget, idx, v0, 0.0f, 0.0f, 0.0f); }
	void ARBProgramObject::SetUniform2f(int idx, float v0, float v1                    ) const { glPEP4f(uniformTarget, idx, v0,   v1, 0.0f, 0.0f); }
	void ARBProgramObject::SetUniform3f(int idx, float v0, float v1, float v2          ) const { glPEP4f(uniformTarget, idx, v0,   v1,   v2, 0.0f); }
	void ARBProgramObject::SetUniform4f(int idx, float v0, float v1, float v2, float v3) const { glPEP4f(uniformTarget, idx, v0,   v1,   v2,   v3); }

	void ARBProgramObject::SetUniform2iv(int idx, const int*   v) const { int   vv[4]; vv[0] = v[0]; vv[1] = v[1]; vv[2] =    0; vv[3] =    0; glPEP4fv(uniformTarget, idx, (float*) vv); }
	void ARBProgramObject::SetUniform3iv(int idx, const int*   v) const { int   vv[4]; vv[0] = v[0]; vv[1] = v[1]; vv[2] = v[2]; vv[3] =    0; glPEP4fv(uniformTarget, idx, (float*) vv); }
	void ARBProgramObject::SetUniform4iv(int idx, const int*   v) const { int   vv[4]; vv[0] = v[0]; vv[1] = v[1]; vv[2] = v[2]; vv[3] = v[3]; glPEP4fv(uniformTarget, idx, (float*) vv); }
	void ARBProgramObject::SetUniform2fv(int idx, const float* v) const { float vv[4]; vv[0] = v[0]; vv[1] = v[1]; vv[2] = 0.0f; vv[3] = 0.0f; glPEP4fv(uniformTarget, idx,          vv); }
	void ARBProgramObject::SetUniform3fv(int idx, const float* v) const { float vv[4]; vv[0] = v[0]; vv[1] = v[1]; vv[2] = v[2]; vv[3] = 0.0f; glPEP4fv(uniformTarget, idx,          vv); }
	void ARBProgramObject::SetUniform4fv(int idx, const float* v) const { float vv[4]; vv[0] = v[0]; vv[1] = v[1]; vv[2] = v[2]; vv[3] = v[3]; glPEP4fv(uniformTarget, idx,          vv); }
	#undef glPEP4f
	#undef glPEP4fv

	void ARBProgramObject::AttachShaderObject(IShaderObject* so) {
		if (so != NULL) {
			IProgramObject::AttachShaderObject(so);
		}
	}



	GLSLProgramObject::GLSLProgramObject(const std::string& poName): IProgramObject(poName) {
		objID = glCreateProgram();
	}

	void GLSLProgramObject::Enable() const { glUseProgram(objID); }
	void GLSLProgramObject::Disable() const { glUseProgram(0); }

	void GLSLProgramObject::Link() {
		glLinkProgram(objID);

		valid = glslIsValid(objID);
		log += glslGetLog(objID);

		if (!IsValid()) {
			LOG_L(L_WARNING, "[%s] linking-error-log in \"%s\":\n%s",
				__FUNCTION__, name.c_str(), GetLog().c_str());
		}
	}

	void GLSLProgramObject::Validate() {
		GLint validated = 0;

		glValidateProgram(objID);
		glGetProgramiv(objID, GL_VALIDATE_STATUS, &validated);

		// append the validation-log
		log += glslGetLog(objID);

		valid = valid && bool(validated);
	}

	void GLSLProgramObject::Release() {
		IProgramObject::Release();

		glDeleteProgram(objID);
	}

	void GLSLProgramObject::Reload() {
	#ifdef USE_GML
		//FIXME GLSLCopyState() isn't supported by GML yet
		{
			LOG_L(L_WARNING, "/ReloadShaders isn't available in spring-mt (use default build)");
			return;
		}
	#endif

		log = "";
		valid = false;

		if (GetAttachedShaderObjs().empty()) {
			return;
		}

		GLuint oldProgID = objID;
		
		for (SOVecIt it = GetAttachedShaderObjs().begin(); it != GetAttachedShaderObjs().end(); ++it) {
			glDetachShader(oldProgID, (*it)->GetObjID());
		}
		for (SOVecIt it = GetAttachedShaderObjs().begin(); it != GetAttachedShaderObjs().end(); ++it) {
			(*it)->Release();
			(*it)->Compile();
		}

		objID = glCreateProgram();
		for (SOVecIt it = GetAttachedShaderObjs().begin(); it != GetAttachedShaderObjs().end(); ++it) {
			if ((*it)->IsValid()) {
				glAttachShader(objID, (*it)->GetObjID());
			}
		}

		Link();
		GLSLCopyState(objID, oldProgID);
		glDeleteProgram(oldProgID);
	}

	void GLSLProgramObject::AttachShaderObject(IShaderObject* so) {
		if (so != NULL) {
			assert(!IsShaderAttached(so));
			IProgramObject::AttachShaderObject(so);

			if (so->IsValid()) {
				glAttachShader(objID, so->GetObjID());
			}
		}
	}

	void GLSLProgramObject::SetUniformLocation(const std::string& name) {
		uniformLocs.push_back(glGetUniformLocation(objID, name.c_str()));
	}

	void GLSLProgramObject::SetUniform1i(int idx, int   v0                              ) const { glUniform1i(uniformLocs[idx], v0            ); }
	void GLSLProgramObject::SetUniform2i(int idx, int   v0, int   v1                    ) const { glUniform2i(uniformLocs[idx], v0, v1        ); }
	void GLSLProgramObject::SetUniform3i(int idx, int   v0, int   v1, int   v2          ) const { glUniform3i(uniformLocs[idx], v0, v1, v2    ); }
	void GLSLProgramObject::SetUniform4i(int idx, int   v0, int   v1, int   v2, int   v3) const { glUniform4i(uniformLocs[idx], v0, v1, v2, v3); }
	void GLSLProgramObject::SetUniform1f(int idx, float v0                              ) const { glUniform1f(uniformLocs[idx], v0            ); }
	void GLSLProgramObject::SetUniform2f(int idx, float v0, float v1                    ) const { glUniform2f(uniformLocs[idx], v0, v1        ); }
	void GLSLProgramObject::SetUniform3f(int idx, float v0, float v1, float v2          ) const { glUniform3f(uniformLocs[idx], v0, v1, v2    ); }
	void GLSLProgramObject::SetUniform4f(int idx, float v0, float v1, float v2, float v3) const { glUniform4f(uniformLocs[idx], v0, v1, v2, v3); }

	void GLSLProgramObject::SetUniform2iv(int idx, const int*   v) const { glUniform2iv(uniformLocs[idx], 1, v); }
	void GLSLProgramObject::SetUniform3iv(int idx, const int*   v) const { glUniform3iv(uniformLocs[idx], 1, v); }
	void GLSLProgramObject::SetUniform4iv(int idx, const int*   v) const { glUniform4iv(uniformLocs[idx], 1, v); }
	void GLSLProgramObject::SetUniform2fv(int idx, const float* v) const { glUniform2fv(uniformLocs[idx], 1, v); }
	void GLSLProgramObject::SetUniform3fv(int idx, const float* v) const { glUniform3fv(uniformLocs[idx], 1, v); }
	void GLSLProgramObject::SetUniform4fv(int idx, const float* v) const { glUniform4fv(uniformLocs[idx], 1, v); }

	void GLSLProgramObject::SetUniformMatrix2fv(int idx, bool transp, const float* v) const { glUniformMatrix2fv(uniformLocs[idx], 1, transp, v); }
	void GLSLProgramObject::SetUniformMatrix3fv(int idx, bool transp, const float* v) const { glUniformMatrix3fv(uniformLocs[idx], 1, transp, v); }
	void GLSLProgramObject::SetUniformMatrix4fv(int idx, bool transp, const float* v) const { glUniformMatrix4fv(uniformLocs[idx], 1, transp, v); }
	#ifdef glUniformMatrix2dv
	void GLSLProgramObject::SetUniformMatrix2dv(int idx, bool transp, const double* v) const { glUniformMatrix2dv(uniformLocs[idx], 1, transp, v); }
	#else
	void GLSLProgramObject::SetUniformMatrix2dv(int idx, bool transp, const double* v) const {}
	#endif
	#ifdef glUniformMatrix3dv
	void GLSLProgramObject::SetUniformMatrix3dv(int idx, bool transp, const double* v) const { glUniformMatrix3dv(uniformLocs[idx], 1, transp, v); }
	#else
	void GLSLProgramObject::SetUniformMatrix3dv(int idx, bool transp, const double* v) const {}
	#endif
	#ifdef glUniformMatrix4dv
	void GLSLProgramObject::SetUniformMatrix4dv(int idx, bool transp, const double* v) const { glUniformMatrix4dv(uniformLocs[idx], 1, transp, v); }
	#else
	void GLSLProgramObject::SetUniformMatrix4dv(int idx, bool transp, const double* v) const {}
	#endif

	void GLSLProgramObject::SetUniformMatrixArray4fv(int idx, int count, bool transp, const float* v) const { glUniformMatrix4fv(uniformLocs[idx], count, transp, v); }
	#ifdef glUniformMatrix4dv
	void GLSLProgramObject::SetUniformMatrixArray4dv(int idx, int count, bool transp, const double* v) const { glUniformMatrix4dv(uniformLocs[idx], count, transp, v); }
	#else
	void GLSLProgramObject::SetUniformMatrixArray4dv(int idx, int count, bool transp, const double* v) const {}
	#endif
}
