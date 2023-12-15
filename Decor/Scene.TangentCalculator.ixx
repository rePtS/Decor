module;

#include <DirectXMath.h>
#include <cstddef>

export module Scene.TangentCalculator;

import MikkTSpace;

export class ITangentCalculable
{
public:
    virtual size_t GetFacesCount() const = 0;
    virtual size_t GetVerticesPerFace() const = 0;
    virtual void GetPosition(float outpos[], const int face, const int vertex) const = 0;
    virtual void GetNormal(float outnormal[], const int face, const int vertex) const = 0;
    virtual void GetTextCoord(float outuv[], const int face, const int vertex) const = 0;
    virtual void SetTangent(const float intangent[], const float sign, const int face, const int vertex) = 0;
};

namespace TangentCalculator
{
    ITangentCalculable& GetPrimitive(const SMikkTSpaceContext* context)
    {
        return *static_cast<ITangentCalculable*>(context->m_pUserData);
    }

    int getNumFaces(const SMikkTSpaceContext* context)
    {
        return (int)GetPrimitive(context).GetFacesCount();
    }

    int getNumVerticesOfFace(const SMikkTSpaceContext* context,
        const int face)
    {
        face; // unused param

        return (int)GetPrimitive(context).GetVerticesPerFace();
    }

    void getPosition(const SMikkTSpaceContext* context,
        float outpos[],
        const int face,
        const int vertex)
    {
        GetPrimitive(context).GetPosition(outpos, face, vertex);
    }

    void getNormal(const SMikkTSpaceContext* context,
        float outnormal[],
        const int face,
        const int vertex)
    {
        GetPrimitive(context).GetNormal(outnormal, face, vertex);
    }

    void getTexCoord(const SMikkTSpaceContext* context,
        float outuv[],
        const int face,
        const int vertex)
    {
        GetPrimitive(context).GetTextCoord(outuv, face, vertex);
    }

    void setTSpaceBasic(const SMikkTSpaceContext* context,
        const float tangent[],
        const float sign,
        const int face,
        const int vertex)
    {
        GetPrimitive(context).SetTangent(tangent, sign, face, vertex);
    }

    export bool Calculate(ITangentCalculable& primitive)
    {
        SMikkTSpaceInterface iface;
        iface.m_getNumFaces = getNumFaces;
        iface.m_getNumVerticesOfFace = getNumVerticesOfFace;
        iface.m_getPosition = getPosition;
        iface.m_getNormal = getNormal;
        iface.m_getTexCoord = getTexCoord;
        iface.m_setTSpaceBasic = setTSpaceBasic;
        iface.m_setTSpace = nullptr;

        SMikkTSpaceContext context;
        context.m_pInterface = &iface;
        context.m_pUserData = &primitive;

        return genTangSpaceDefault(&context) == 1;
    }
}