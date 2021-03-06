/**
 * StOutDistorted, class providing stereoscopic output in anamorph side by side format using StCore toolkit.
 * Copyright © 2016 Kirill Gavrilov <kirill@sview.ru>
 *
 * Distributed under the Boost Software License, Version 1.0.
 * See accompanying file license-boost.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt
 */

#ifndef __StProgramFlat_h_
#define __StProgramFlat_h_

#include <StGL/StGLProgram.h>

/**
 * Flat GLSL program.
 */
class StProgramFlat : public StGLProgram {

        public:

    ST_LOCAL StProgramFlat();

    /**
     * Position vertex attribute location.
     */
    ST_LOCAL StGLVarLocation getVVertexLoc()   const { return StGLVarLocation(0); }

    /**
     * Texture vertex attribute location.
     */
    ST_LOCAL StGLVarLocation getVTexCoordLoc() const { return StGLVarLocation(1); }

    /**
     * Initialize the program.
     */
    ST_LOCAL virtual bool init(StGLContext& theCtx) ST_ATTR_OVERRIDE;

};

#endif // __StProgramFlat_h_
