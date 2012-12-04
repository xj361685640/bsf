/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2011 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/

#ifndef __ATI_FS_GLGpuProgram_H__
#define __ATI_FS_GLGpuProgram_H__

#include "CmGLPrerequisites.h"
#include "CmGLGpuProgram.h"

namespace CamelotEngine {

	/** Specialisation of the GL low-level program for ATI Fragment Shader programs. */
	class CM_RSGL_EXPORT ATI_FS_GLGpuProgram : public GLGpuProgram
	{
	public:
		virtual ~ATI_FS_GLGpuProgram();


		/// Execute the binding functions for this program
		void bindProgram(void);
		/// Execute the unbinding functions for this program
		void unbindProgram(void);
		/// Execute the param binding functions for this program
		void bindProgramParameters(GpuProgramParametersSharedPtr params, UINT16 mask);

		/// Get the assigned GL program id
		const GLuint getProgramID(void) const
		{ return mProgramID; }

	protected:
		friend GpuProgram* createGL_ATI_FS_GpuProgram(GpuProgramType gptype, const String& syntaxCode);

		ATI_FS_GLGpuProgram();

		/// @copydoc Resource::unload
		void unloadImpl(void);
		void loadFromSource(void);

	}; // class ATI_FS_GLGpuProgram



}; // namespace CamelotEngine

#endif // __ATI_FS_GLGpuProgram_H__
