#include "Types.h"

using namespace OpenFlight;

//------------------------------------------------------------------------------
//--- Matrix4f
//------------------------------------------------------------------------------
Matrix4f::Matrix4f()
{
    mInternalStorage = isRowMajor;
    mData[0][0] = 1; mData[0][1] = 0; mData[0][2] = 0; mData[0][3] = 0;
    mData[1][0] = 0; mData[1][1] = 1; mData[1][2] = 0; mData[1][3] = 0;
    mData[2][0] = 0; mData[2][1] = 0; mData[2][2] = 1; mData[2][3] = 0;
    mData[3][0] = 0; mData[3][1] = 0; mData[3][2] = 0; mData[3][3] = 1;
}

//------------------------------------------------------------------------------
//--- Vertex
//------------------------------------------------------------------------------

//-------------------------------------------------------------------------
bool Vertex::hasFlag(Vertex::flag iFlag) const
{
    //not implemented
    return false;
}


