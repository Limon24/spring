/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#ifndef LUA_MATERIAL_H
#define LUA_MATERIAL_H

#include <string>
#include <vector>

/*
LuaMaterial
	LuaMatShader
	LuaMatUniforms
	LuaMatTex
LuaMatBin : public LuaMaterial
	array of units & features that use the Material
	refCounter

class LuaMatHandler
	LuaMatBin* []

LuaMatRef
	LuaMatBin*

LuaObjectLODMaterial rename
	LuaMatRef
	pre + post DL

LuaObjectMaterial (set of LODs) //FIXME merge with LuaObjectLODMaterial?
	LuaObjectLODMaterial []

LuaObjectMaterialData (helper to choose the current LOD) //FIXME rename
	LuaObjectMaterial []
*/


#include "LuaOpenGLUtils.h"
#include "LuaObjectMaterial.h" // for LuaMatRef
#include "Rendering/GL/myGL.h"
#include "System/UnorderedMap.hpp"


class CSolidObject;

/******************************************************************************/
/******************************************************************************/

class LuaMatShader {
	public:
		enum Type {
			LUASHADER_3DO    = 0, // engine default; *must* equal MODELTYPE_3DO!
			LUASHADER_S3O    = 1, // engine default; *must* equal MODELTYPE_S3O!
			LUASHADER_ASS    = 2, // engine default; *must* equal MODELTYPE_ASS!
			LUASHADER_GL     = 3, // custom Lua; *must* equal MODELTYPE_OTHER
			LUASHADER_LAST   = 4,
		};
		enum Pass {
			LUASHADER_PASS_FWD = 0, // forward pass
			LUASHADER_PASS_DFR = 1, // deferred pass
			LUASHADER_PASS_CNT = 2,
		};

	public:
		void Finalize() { openglID *= (type == LUASHADER_GL); }
		void Execute(const LuaMatShader& prev, bool deferredPass) const;
		void Print(const std::string& indent, bool isDeferred) const;

		// only accepts custom programs
		void SetCustomTypeFromID(unsigned int id) {
			if ((openglID = id) == 0) {
				type = LuaMatShader::LUASHADER_LAST;
			} else {
				type = LuaMatShader::LUASHADER_GL;
			}
		}
		// only accepts engine programs
		void SetEngineTypeFromKey(const std::string& key) {
			if (key == "3do") { type = LUASHADER_3DO; return; }
			if (key == "s3o") { type = LUASHADER_S3O; return; }
			if (key == "ass") { type = LUASHADER_ASS; return; }
			type = LUASHADER_LAST;
		}

		static int Compare(const LuaMatShader& a, const LuaMatShader& b);

		bool operator<(const LuaMatShader& mt) const { return (Compare(*this, mt)  < 0); }
		bool operator==(const LuaMatShader& mt) const = delete;
		bool operator!=(const LuaMatShader& mt) const = delete;

		// both passes require a (custom or engine) shader
		bool ValidForPass(Pass pass) const { return (type != LUASHADER_LAST); }

		bool IsCustomType() const { return (type == LUASHADER_GL); }
		bool IsEngineType() const { return (type >= LUASHADER_3DO && type <= LUASHADER_ASS); }

	public:
		Type type = LUASHADER_LAST;
		GLuint openglID = 0;
};


/******************************************************************************/

struct LuaMatUniforms {
public:
	void Print(const std::string& indent, bool isDeferred) const;
	void Parse(lua_State* L, const int tableIdx);
	void AutoLink(LuaMatShader* s);
	void Validate(LuaMatShader* s);

	void Execute() const;
	void ExecuteInstanceTeamColor(const float4& tc) const { teamColor.Execute(tc); }
	void ExecuteInstanceMatrices(const CMatrix44f& mm, const std::vector<CMatrix44f>& pms) const {
		modelMatrix.Execute(mm);
		pieceMatrices.Execute(pms);
	}

	static int Compare(const LuaMatUniforms& a, const LuaMatUniforms& b);

public:
	struct IUniform {
		static_assert(GL_INVALID_INDEX == -1, "glGetUniformLocation is defined to return -1 (GL_INVALID_INDEX) for invalid names");

		IUniform(): loc(GL_INVALID_INDEX) {}

		virtual bool CanExec() const = 0;
		virtual GLenum GetType() const = 0;

		bool SetLoc(GLint i) { return ((loc = i) != GL_INVALID_INDEX); }
		bool IsValid() const { return (loc != GL_INVALID_INDEX); }

	public:
		GLint loc;
	};

private:
	template<typename Type> struct UniformMat : public IUniform {
	public:
		bool CanExec() const { return (loc != GL_INVALID_INDEX); }
		bool Execute(const Type& val) const { return (CanExec() && RawExec(val)); }
		bool RawExec(const Type& val) const { glUniformMatrix4fv(loc, 1, GL_FALSE, val); return true; }

		GLenum GetType() const { return GL_FLOAT_MAT4; }
	};

	template<typename Type> struct UniformMatArray : public IUniform {
	public:
		bool CanExec() const { return (loc != GL_INVALID_INDEX); }
		bool Execute(const Type& val) const { return (CanExec() && RawExec(val)); }
		bool RawExec(const Type& val) const { glUniformMatrix4fv(loc, val.size(), GL_FALSE, val[0]); return true; }

		GLenum GetType() const { return GL_FLOAT_MAT4; }
	};

	template<typename Type, size_t Size = sizeof(Type) / sizeof(float)> struct UniformVec : public IUniform {
	public:
		bool CanExec() const { return (loc != GL_INVALID_INDEX); }
		bool Execute(const Type val) const { return (CanExec() && RawExec(val)); }
		bool RawExec(const Type val) const {
			switch (Size) {
				case 3: { glUniform3fv(loc, 1, &val.x); return true; } break;
				case 4: { glUniform4fv(loc, 1, &val.x); return true; } break;
				default: { return false; } break;
			}
		}

		GLenum GetType() const {
			switch (Size) {
				case 3: return GL_FLOAT_VEC3;
				case 4: return GL_FLOAT_VEC4;
				default: return 0;
			}
		}
	};

	template<typename Type> struct UniformInt : public IUniform {
	public:
		UniformInt(): cur(Type(0)), prv(Type(0)) {}
		bool CanExec() const { return (loc != -1 && cur != prv); } //FIXME a shader might be bound to multiple materials in that case we cannot rely on this!
		bool Execute(const Type val) { cur = val; return (CanExec() && RawExec(val)); }
		bool RawExec(const Type val) { glUniform1i(loc, prv = val); return true; }

		GLenum GetType() const { return GL_INT; }
	public:
		Type cur;
		Type prv;
	};

private:
	spring::unsynced_map<IUniform*, std::string> GetEngineUniformNamePairs();
	spring::unsynced_map<std::string, IUniform*> GetEngineNameUniformPairs();

public:
	UniformMat<CMatrix44f> viewMatrix;
	UniformMat<CMatrix44f> projMatrix;
	UniformMat<CMatrix44f> viprMatrix;
	UniformMat<CMatrix44f> viewMatrixInv;
	UniformMat<CMatrix44f> projMatrixInv;
	UniformMat<CMatrix44f> viprMatrixInv;

	UniformMat<CMatrix44f> modelMatrix;
	UniformMatArray< std::vector<CMatrix44f> > pieceMatrices;

	UniformMat<CMatrix44f> shadowMatrix;
	UniformVec<float4> shadowParams;

	UniformVec<float4> teamColor;

	UniformVec<float3> camPos;
	UniformVec<float3> camDir;
	UniformVec<float3> sunDir;
	UniformVec<float3> rndVec;

	mutable UniformInt<         int> simFrame;
	mutable UniformInt<unsigned int> visFrame;
};


class LuaMaterial {
	public:
		LuaMaterial() = default;
		LuaMaterial(LuaMatType matType): type(matType) {}

		void Parse(
			lua_State* L,
			const int tableIdx,
			void(*ParseShader)(lua_State*, int, LuaMatShader&),
			void(*ParseTexture)(lua_State*, int, LuaMatTexture&),
			GLuint(*ParseDisplayList)(lua_State*, int)
		);

		void Finalize();
		void Execute(const LuaMaterial& prev, bool deferredPass) const;

		void ExecuteInstanceTeamColor(const float4& tc, bool deferredPass) const { uniforms[deferredPass].ExecuteInstanceTeamColor(tc); }
		void ExecuteInstanceMatrices(const CMatrix44f& mm, const std::vector<CMatrix44f>& pms, bool deferredPass) const {
			uniforms[deferredPass].ExecuteInstanceMatrices(mm, pms);
		}

		void Print(const std::string& indent) const;

		static int Compare(const LuaMaterial& a, const LuaMaterial& b);

		bool operator <  (const LuaMaterial& m) const { return (Compare(*this, m) <  0); }
		bool operator == (const LuaMaterial& m) const { return (Compare(*this, m) == 0); }

	public:
		static const int MAX_TEX_UNITS = 16;

		// default invalid
		LuaMatType type = LuaMatType(-1);

		int order = 0; // for manually adjusting rendering order
		int texCount = 0;

		// [0] := standard, [1] := deferred
		LuaMatShader   shaders[LuaMatShader::LUASHADER_PASS_CNT];
		LuaMatUniforms uniforms[LuaMatShader::LUASHADER_PASS_CNT];
		LuaMatTexture  textures[MAX_TEX_UNITS];

		GLenum cullingMode = 0;

		static const LuaMaterial defMat;
};


/******************************************************************************/
/******************************************************************************/

class LuaMatBin : public LuaMaterial {

	friend class LuaMatHandler;

	public:
		void ClearUnits() { units.clear(); }
		void ClearFeatures() { features.clear(); }

		const std::vector<CSolidObject*>& GetUnits() const { return units; }
		const std::vector<CSolidObject*>& GetFeatures() const { return features; }
		const std::vector<CSolidObject*>& GetObjects(LuaObjType objType) const {
			static const std::vector<CSolidObject*> dummy;

			switch (objType) {
				case LUAOBJ_UNIT   : { return (GetUnits   ()); } break;
				case LUAOBJ_FEATURE: { return (GetFeatures()); } break;
				default            : {          assert(false); } break;
			}

			return dummy;
		}

		void Ref() { refCount++; }
		void UnRef();

		void AddUnit(CSolidObject* o) { units.push_back(o); }
		void AddFeature(CSolidObject* o) { features.push_back(o); }
		void AddObject(CSolidObject* o, LuaObjType objType) {
			switch (objType) {
				case LUAOBJ_UNIT   : { AddUnit   (o); } break;
				case LUAOBJ_FEATURE: { AddFeature(o); } break;
				default            : { assert(false); } break;
			}
		}

		void Print(const std::string& indent) const;

	private:
		LuaMatBin(const LuaMaterial& _mat) : LuaMaterial(_mat), refCount(0) {}
		LuaMatBin(const LuaMatBin&);
		LuaMatBin& operator=(const LuaMatBin&) = delete;

	private:
		int refCount;

		std::vector<CSolidObject*> units;
		std::vector<CSolidObject*> features;
};


/******************************************************************************/

// typedef std::set<LuaMatBin*, LuaMatBinPtrLessThan> LuaMatBinSet;
typedef std::vector<LuaMatBin*> LuaMatBinSet;


/******************************************************************************/

class LuaMatHandler {
	public:
		LuaMatRef GetRef(const LuaMaterial& mat);

		const LuaMatBinSet& GetBins(LuaMatType type) const { return binTypes[type]; }

		void ClearBins(LuaObjType objType);
		void ClearBins(LuaObjType objType, LuaMatType matType);

		void FreeBin(LuaMatBin* bin);

		void PrintBins(const std::string& indent, LuaMatType type) const;
		void PrintAllBins(const std::string& indent) const;

	public:
		typedef void (*SetupDrawStateFunc)(unsigned int, bool);
		typedef void (*ResetDrawStateFunc)(unsigned int, bool);

		// note: only the DEF_* entries can be non-NULL
		SetupDrawStateFunc setupDrawStateFuncs[LuaMatShader::LUASHADER_LAST];
		ResetDrawStateFunc resetDrawStateFuncs[LuaMatShader::LUASHADER_LAST];

	private:
		LuaMatHandler();
		LuaMatHandler(const LuaMatHandler&);
		LuaMatHandler operator=(const LuaMatHandler&);
		~LuaMatHandler();

	private:
		LuaMatBinSet binTypes[LUAMAT_TYPE_COUNT];

	public:
		static LuaMatHandler handler;
};


/******************************************************************************/
/******************************************************************************/


extern LuaMatHandler& luaMatHandler;


#endif /* LUA_MATERIAL_H */
