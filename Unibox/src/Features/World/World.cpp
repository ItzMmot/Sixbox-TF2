#include "../World/World.h"

#include <numeric>

class CEntityEnumerator : public IPartitionEnumerator
{
public:
	IterationRetval_t EnumElement(IHandleEntity* pHandleEntity)
	{
		m_vEntities.push_back(pHandleEntity);
		return ITERATION_CONTINUE;
	}

	std::vector<IHandleEntity*>	m_vEntities = {};
};

void CWorld::SetNormalValidCallback(NormalValidCallback* pCallback)
{
	m_pNormalValidCallback = pCallback ? pCallback : &s_fNormalValid;
}

bool CWorld::FaceValid(const std::vector<Vec3>& vVertices, const Vec3* pNormal, const Vec3* pMins, const Vec3* pMaxs)
{
	if (!(*m_pVerticesValidCallback)(vVertices))
		return false;

	if (pMins && pMaxs && !(*m_pBoundsValidCallback)(vVertices, *pMins, *pMaxs))
		return false;

	if (pNormal && !(*m_pNormalValidCallback)(vVertices, *pNormal))
		return false;

	return true;
}



static CPhysConvex* s_aConvexes[1024];
static inline void GetFacesForConvex(CPhysConvex* pConvex, std::vector<Face_t>& vFaces, float flResize = 0.f, int8_t iType = FaceTypeEnum::None)
{
	CPolyhedron* pPolyhedron = I::PhysicsCollision->PolyhedronFromConvex(pConvex, true);
	if (!pPolyhedron)
		return;

	if (!flResize)
	{
		std::vector<std::pair<std::vector<int>, Vec3>> vTemp = {};
		for (int iPolygon = 0; iPolygon < pPolyhedron->iPolygonCount; iPolygon++)
		{
			Polyhedron_IndexedPolygon_t* pPolygon = &pPolyhedron->pPolygons[iPolygon];

			std::vector<int>* pVertices = nullptr;
			for (auto& [vVertices, vNormal] : vTemp)
			{
				if (pPolygon->polyNormal.Dot(vNormal) > 1 - CALC_EPSILON)
				{
					pVertices = &vVertices;
					break;
				}
			}
			if (!pVertices)
			{
				vTemp.emplace_back(std::vector<int>(), pPolygon->polyNormal);
				pVertices = &vTemp.back().first;
			}

			for (int iLineRef = 0; iLineRef < pPolygon->iIndexCount; iLineRef++)
			{
				Polyhedron_IndexedLineReference_t* pLineReference = &pPolyhedron->pIndices[pPolygon->iFirstIndex + iLineRef];
				Polyhedron_IndexedLine_t* pLine = &pPolyhedron->pLines[pLineReference->iLineIndex];
				int iVertex = pLine->iPointIndices[pLineReference->iEndPointIndex];
				if (std::find(pVertices->begin(), pVertices->end(), iVertex) == pVertices->end())
					pVertices->push_back(iVertex);
			}
		}

		for (auto& [vIndices, vNormal] : vTemp)
		{
			if (vIndices.size() < 3)
				continue;

			std::vector<Vec3> vVertices = {};
			for (auto& iVertex : vIndices)
				vVertices.push_back(pPolyhedron->pVertices[iVertex]);

			if (vVertices.size() > 3)
			{
				Vec3 vCenter = std::reduce(vVertices.begin(), vVertices.end()) / vVertices.size();
				Vec3 vDir1 = (vVertices.front() - vCenter).Normalized();
				Vec3 vDir2 = vDir1.Cross(vNormal);
				std::sort(vVertices.begin(), vVertices.end(), [&](const Vec3& a, const Vec3& b) -> bool
				{
					return atan2f(vDir2.Dot(a - vCenter), vDir1.Dot(a - vCenter)) < atan2f(vDir2.Dot(b - vCenter), vDir1.Dot(b - vCenter));
				});
			}

			vFaces.emplace_back(vVertices, vNormal, iType);
		}
	}
	else
	{	// rough approximation, not too sure why this is needed in the first place
		std::vector<Plane_t> vPlanes = {}; vPlanes.reserve(pPolyhedron->iPolygonCount);
		for (int iPolygon = 0; iPolygon < pPolyhedron->iPolygonCount; iPolygon++)
		{
			Polyhedron_IndexedPolygon_t* pPolygon = &pPolyhedron->pPolygons[iPolygon];

			if (pPolygon->iIndexCount)
			{
				Polyhedron_IndexedLineReference_t* pLineReference = &pPolyhedron->pIndices[pPolygon->iFirstIndex];
				Polyhedron_IndexedLine_t* pLine = &pPolyhedron->pLines[pLineReference->iLineIndex];
				Vec3& vVertex = pPolyhedron->pVertices[pLine->iPointIndices[pLineReference->iEndPointIndex]];
				vPlanes.emplace_back(pPolygon->polyNormal, pPolygon->polyNormal.Dot(vVertex + flResize * pPolygon->polyNormal));
			}
		}

		if (CPhysConvex* pConvex2 = I::PhysicsCollision->ConvexFromPlanes(reinterpret_cast<float*>(vPlanes.data()), int(vPlanes.size()), 0.f))
		{
			GetFacesForConvex(pConvex2, vFaces, 0.f, iType);
			I::PhysicsCollision->ConvexFree(pConvex2);
		}
	}

	pPolyhedron->Release();
}

static inline void GetFacesForCollision(CPhysCollide* pCollision, std::vector<Face_t>& vFaces, float flResize = 0.f, int8_t iType = FaceTypeEnum::None)
{
	int iConvexes = I::PhysicsCollision->GetConvexesUsedInCollideable(pCollision, s_aConvexes, sizeof(s_aConvexes) / sizeof(CPhysConvex*));
	for (int iConvex = 0; iConvex < iConvexes; iConvex++)
		GetFacesForConvex(s_aConvexes[iConvex], vFaces, flResize, iType);

	/*
	Vec3* pVertices; int iVertices = I::PhysicsCollision->CreateDebugMesh(pCollision, &pVertices); iVertices -= iVertices % 3;
	for (int iVertex = 0; iVertex < iVertices; iVertex += 3)
	{
		std::vector<Vec3> vVertices = { pVertices[iVertex], pVertices[iVertex + 1], pVertices[iVertex + 2] };
		Vec3 vNormal = (vVertices[0] - vVertices[2]).Cross(vVertices[0] - vVertices[1]).Normalized();

		vFaces.emplace_back(vVertices, vNormal, iType);
	}
	I::PhysicsCollision->DestroyDebugMesh(iVertices, pVertices);
	*/
}

void CWorld::Cache()
{
	if (!m_iNeedsCache)
		return;

	CacheBoxBrushes();
	CachePlaneBrushes();
	CacheProps();
	CacheEntities();
	// Allocate brush query storage while constructing the map cache. Readers
	// below never resize it or copy the cached polygons into heap containers.
	if (I::BSPData && I::BSPData->numbrushes > 0)
		m_vBrushStorage.resize(I::BSPData->numbrushes);
}

void CWorld::CacheBoxBrushes()
{
	if (!(m_iNeedsCache & FaceTypeEnum::BoxBrush))
		return;

	SDK::Output(__FUNCTION__, "Cache start", {}, OUTPUT_DEBUG);

	if (!I::BSPData || I::BSPData->numbrushes <= 0 || I::BSPData->numboxbrushes <= 0)
	{
		SDK::Output(__FUNCTION__, "BSP not ready, skipping", {}, OUTPUT_DEBUG);
		return;
	}

	m_iNeedsCache &= ~FaceTypeEnum::BoxBrush;

	for (int iBrush = 0; iBrush < I::BSPData->numbrushes; iBrush++)
	{
		cbrush_t* pBrush = &I::BSPData->map_brushes[iBrush];
		if (!pBrush->IsBox())
			continue;

		const int iBoxIdx = pBrush->GetBox();
		if (iBoxIdx < 0 || iBoxIdx >= I::BSPData->numboxbrushes)
			continue;

		cboxbrush_t* pBoxBrush = &I::BSPData->map_boxbrushes[iBoxIdx];

		Vec3 aVertices[8];
		for (int i = 0; i != 8; i++)
		{
			aVertices[i].x = i & (1 << 0) ? pBoxBrush->maxs.x : pBoxBrush->mins.x;
			aVertices[i].y = i & (1 << 1) ? pBoxBrush->maxs.y : pBoxBrush->mins.y;
			aVertices[i].z = i & (1 << 2) ? pBoxBrush->maxs.z : pBoxBrush->mins.z;
		}
		Vec3 aNormals[6] = {
			{ 1, 0, 0 },
			{ -1, 0, 0 },
			{ 0, 1, 0 },
			{ 0, -1, 0 },
			{ 0, 0, 1 },
			{ 0, 0, -1 }
		};

		for (int i = 0; i < 6; i++)
		{
			const int iSurfIdx = pBoxBrush->surfaceIndex[i];
			if (iSurfIdx == 0xFFFF)
				continue;
			if (I::BSPData->map_surfaces[iSurfIdx].flags & SURF_SKY)
				continue;

			std::vector<Vec3> vVertices;
			switch (i)
			{
			case 0: vVertices = { aVertices[1], aVertices[5], aVertices[7], aVertices[3] }; break;
			case 1: vVertices = { aVertices[2], aVertices[6], aVertices[4], aVertices[0] }; break;
			case 2: vVertices = { aVertices[3], aVertices[7], aVertices[6], aVertices[2] }; break;
			case 3: vVertices = { aVertices[0], aVertices[4], aVertices[5], aVertices[1] }; break;
			case 4: vVertices = { aVertices[6], aVertices[7], aVertices[5], aVertices[4] }; break;
			case 5: vVertices = { aVertices[0], aVertices[1], aVertices[3], aVertices[2] }; break;
			}
			Vec3& vNormal = aNormals[i];

			m_mFaceCache[pBrush].emplace_back(vVertices, vNormal, FaceTypeEnum::BoxBrush);
		}
	}

	SDK::Output(__FUNCTION__, "Cache finish", {}, OUTPUT_DEBUG);
}

void CWorld::CachePlaneBrushes()
{
	if (!(m_iNeedsCache & FaceTypeEnum::PlaneBrush))
		return;

	SDK::Output(__FUNCTION__, "Cache start", {}, OUTPUT_DEBUG);

	if (!I::BSPData || I::BSPData->numbrushes <= 0 || I::BSPData->numbrushsides <= 0)
	{
		SDK::Output(__FUNCTION__, "BSP not ready, skipping", {}, OUTPUT_DEBUG);
		return;
	}

	m_iNeedsCache &= ~FaceTypeEnum::PlaneBrush;

	for (int iBrush = 0; iBrush < I::BSPData->numbrushes; iBrush++)
	{
		cbrush_t* pBrush = &I::BSPData->map_brushes[iBrush];
		if (pBrush->IsBox())
			continue;

		std::vector<Plane_t> vPlanes = {}; vPlanes.reserve(pBrush->numsides);
		for (int iSide = 0; iSide != pBrush->numsides; iSide++)
		{
			const int iSideIdx = pBrush->firstbrushside + iSide;
			if (iSideIdx < 0 || iSideIdx >= I::BSPData->numbrushsides)
				continue;

			cbrushside_t* pBrushSide = &I::BSPData->map_brushsides[iSideIdx];
			const int iSurfIdx = pBrushSide->surfaceIndex;
			if (iSurfIdx == 0xFFFF)
				continue;
			if (I::BSPData->map_surfaces[iSurfIdx].flags & SURF_SKY)
				continue;

			cplane_t* pPlane = pBrushSide->plane;
			if (!pPlane)
				continue;
			vPlanes.emplace_back(pPlane->normal, pPlane->dist);
		}
		if (vPlanes.empty())
			continue;

		CPhysConvex* pConvex = I::PhysicsCollision->ConvexFromPlanes(reinterpret_cast<float*>(vPlanes.data()), int(vPlanes.size()), 0.f);
		if (!pConvex)
			continue;

		GetFacesForConvex(pConvex, m_mFaceCache[pBrush], 0.f, FaceTypeEnum::PlaneBrush);
		I::PhysicsCollision->ConvexFree(pConvex);
	}

	SDK::Output(__FUNCTION__, "Cache finish", {}, OUTPUT_DEBUG);
}

void CWorld::CacheProps()
{
	if (!(m_iNeedsCache & FaceTypeEnum::Prop))
		return;

	SDK::Output(__FUNCTION__, "Cache start", {}, OUTPUT_DEBUG);

	if (!I::MDLCache || !I::MDLCache->m_bConnected)
	{
		SDK::Output(__FUNCTION__, "MDLCache not ready, skipping", {}, OUTPUT_DEBUG);
		return;
	}

	m_iNeedsCache &= ~FaceTypeEnum::Prop;

	for (int i = I::MDLCache->m_MDLDict.First(); i != I::MDLCache->m_MDLDict.InvalidIndex(); i = I::MDLCache->m_MDLDict.Next(i))
	{
		studiodata_t* pStudioData = I::MDLCache->m_MDLDict[i];
		if (!pStudioData)
			continue;

		vcollide_t* pCollide = &pStudioData->m_VCollisionData;
		if (m_mFaceCache.contains(pCollide))
			continue;

		if (pCollide->solidCount <= 0)
			continue;

		for (int iSolid = 0; iSolid < pCollide->solidCount; iSolid++)
		{
			CPhysCollide* pCollision = pCollide->solids[iSolid];
			if (!pCollision)
				continue;

			GetFacesForCollision(pCollision, m_mFaceCache[pCollide], 0.f, FaceTypeEnum::Prop);
		}
	}

	SDK::Output(__FUNCTION__, "Cache finish", {}, OUTPUT_DEBUG);
}

void CWorld::CacheEntities()
{
	if (!(m_iNeedsCache & FaceTypeEnum::Entity))
		return;

	SDK::Output(__FUNCTION__, "Cache start", {}, OUTPUT_DEBUG);

	if (!I::BSPData || I::BSPData->numcmodels <= 0)
	{
		SDK::Output(__FUNCTION__, "BSP not ready, skipping", {}, OUTPUT_DEBUG);
		return;
	}

	m_iNeedsCache &= ~FaceTypeEnum::Entity;

	for (int iCModel = 0; iCModel < I::BSPData->numcmodels; iCModel++)
	{
		cmodel_t* pCModel = &I::BSPData->map_cmodels[iCModel];
		vcollide_t* pCollide = &pCModel->vcollisionData;

		if (pCollide->solidCount <= 0)
			continue;

		for (int iSolid = 0; iSolid < pCollide->solidCount; iSolid++)
		{
			CPhysCollide* pCollision = pCollide->solids[iSolid];
			if (!pCollision)
				continue;

			GetFacesForCollision(pCollision, m_mFaceCache[pCollide], 0.5f, FaceTypeEnum::Entity);
		}
	}

	SDK::Output(__FUNCTION__, "Cache finish", {}, OUTPUT_DEBUG);
}



void CWorld::Uncache()
{
	if (++m_iGeometryRevision==0) ++m_iGeometryRevision;
	if (m_iNeedsCache == FaceTypeEnum::Cache)
		return;

	m_mFaceCache.clear();

	m_iNeedsCache = FaceTypeEnum::Cache;
}

std::vector<Face_t> CWorld::GetFacesInAABB(const Vec3& vMins, const Vec3& vMaxs, int iMask, ITraceFilter* pFilter, int iFlags)
{
	Cache();

	std::vector<Face_t> vFaces;

	if ((iFlags & (FaceTypeEnum::BoxBrush | FaceTypeEnum::PlaneBrush)) && I::BSPData && I::BSPData->numbrushes > 0)
	{
		m_vBrushStorage.resize(I::BSPData->numbrushes);
		CUtlVector<int> vBrushes(m_vBrushStorage.data(), static_cast<int>(m_vBrushStorage.size()));
		I::EngineTrace->GetBrushesInAABB(vMins, vMaxs, &vBrushes, iMask);
		for (int iBrush = 0; iBrush < vBrushes.Count(); iBrush++)
		{
			cbrush_t* pBrush = &I::BSPData->map_brushes[vBrushes[iBrush]];
			if (!m_mFaceCache.contains(pBrush))
				continue;

			bool bBox = pBrush->IsBox();
			if (!(iFlags & (bBox ? FaceTypeEnum::BoxBrush : FaceTypeEnum::PlaneBrush)))
				continue;

			auto& vCache = m_mFaceCache[pBrush];
			if (bBox)
			{
				for (auto& tFace : vCache)
				{
					if (FaceValid(tFace.m_vVertices, &tFace.m_vNormal, &vMins, &vMaxs))
					{
						vFaces.push_back(tFace);
						for (auto& vVertex : vFaces.back().m_vVertices)
							vVertex = vVertex.Clamp(vMins, vMaxs);
					}
				}
			}
			else
			{
				for (auto& tFace : vCache)
				{
					if (FaceValid(tFace.m_vVertices, &tFace.m_vNormal, &vMins, &vMaxs))
						vFaces.push_back(tFace);
				}
			}
		}
	}

	if (iFlags & FaceTypeEnum::Displacement && *I::DispCollTrees)
	{
		std::vector<Vec3>& vVertices = m_vDispVertices;
		for (int iDisplacement = 0; iDisplacement < *I::DispCollTreeCount; iDisplacement++)
		{
			CDispCollTree* pDispTree = &(*I::DispCollTrees)[iDisplacement];
			if (!Math::IsBoxIntersectingBox(vMins, vMaxs, pDispTree->m_mins, pDispTree->m_maxs))
				continue;

			int iFaces = pDispTree->GetFaceCount();
			for (int iFace = 0; iFace < iFaces; iFace++)
			{
				CDispCollTri* pTri = &pDispTree->m_aTris[iFace];

				vVertices[0] = pDispTree->m_aVerts[pTri->GetVert(0)];
				vVertices[1] = pDispTree->m_aVerts[pTri->GetVert(1)];
				vVertices[2] = pDispTree->m_aVerts[pTri->GetVert(2)];

				if (FaceValid(vVertices, &pTri->m_vecNormal, &vMins, &vMaxs))
					vFaces.emplace_back(vVertices, pTri->m_vecNormal, FaceTypeEnum::Displacement);
			}
		}
	}

	if (iFlags & (FaceTypeEnum::Entity | FaceTypeEnum::Prop))
	{
		CEntityEnumerator tEnumerator; I::SpatialPartition->EnumerateElementsInBox(I::EngineTrace->SpatialPartitionMask(), vMins, vMaxs, false, &tEnumerator);
		for (auto pHandleEntity : tEnumerator.m_vEntities)
		{
			if (!pHandleEntity)
				continue;
			if (pFilter && !pFilter->ShouldHitEntity(pHandleEntity, iMask))
				continue;

			bool bProp = I::StaticPropMgr->IsStaticProp(pHandleEntity);
			if (!(iFlags & (bProp ? FaceTypeEnum::Prop : FaceTypeEnum::Entity)))
				continue;

			ICollideable* pCollideable = nullptr;
			if (!bProp)
			{
				CBaseEntity* pEntity = reinterpret_cast<CBaseEntity*>(pHandleEntity);
				if (!pEntity->GetAbsVelocity().IsZero())
					continue;

				pCollideable = pEntity->GetCollideable();
			}
			else
				pCollideable = I::StaticPropMgr->GetStaticProp(pHandleEntity);
			if (!pCollideable || !pCollideable->IsSolid())
				continue;

			vcollide_t* pCollide = I::ModelInfoClient->GetVCollide(pCollideable->GetCollisionModel());
			if (!m_mFaceCache.contains(pCollide))
				continue; // cache as well?

			auto& vCache = m_mFaceCache[pCollide];
			for (auto& tFace : vCache)
			{
				std::vector<Vec3> vVertices = tFace.m_vVertices;
				Vec3 vNormal = tFace.m_vNormal;
				if (!pCollideable->GetCollisionAngles())
				{
					for (auto& vVertex : vVertices)
						vVertex += pCollideable->GetCollisionOrigin();
				}
				else
				{
					for (auto& vVertex : vVertices)
						vVertex = Math::VectorTransform(vVertex, pCollideable->CollisionToWorldTransform());
					vNormal = (vVertices[0] - vVertices[2]).Cross(vVertices[0] - vVertices[1]).Normalized();
				}

				if (FaceValid(vVertices, &vNormal, &vMins, &vMaxs))
					vFaces.emplace_back(vVertices, vNormal, tFace.m_iType);
			}
		}
	}

	return vFaces;
}

int CWorld::GatherSeamFaces(const Vec3& mins,const Vec3& maxs,SeamGeometry::Face* output,int capacity)
{
	if (!output || capacity <= 0) return 0;
	capacity = (std::min)(capacity,SeamGeometry::MaxFaces);
	const Vec3 centre=(mins+maxs)*.5f;
	std::array<float,SeamGeometry::MaxFaces> distances{};
	int count=0;
	auto add=[&](const Vec3* vertices,int size,Vec3 normal,const matrix3x4* transform=nullptr) {
		if (!vertices || size<3 || size>SeamGeometry::MaxVertices) return;
		SeamGeometry::Face face;face.count=size;
		Vec3 low=Vec3::GetMax(),high=Vec3::GetMin();
		for (int i=0;i<size;++i)
		{
			const Vec3 vertex=transform?Math::VectorTransform(vertices[i],*transform):vertices[i];
			low=low.Min(vertex);high=high.Max(vertex);
			face.vertices[i]={vertex.x,vertex.y,vertex.z};
		}
		if (!Math::IsBoxIntersectingBox(low,high,mins,maxs)) return;
		if (transform)
		{
			const auto a=SeamGeometry::Sub(face.vertices[1],face.vertices[0]);
			const auto b=SeamGeometry::Sub(face.vertices[2],face.vertices[0]);
			const auto cross=SeamGeometry::Cross(a,b);
			// Rotate the original outward normal; polygon winding is independent.
			const Vec3 rotated={(*transform)[0][0]*normal.x+(*transform)[0][1]*normal.y+(*transform)[0][2]*normal.z,
				(*transform)[1][0]*normal.x+(*transform)[1][1]*normal.y+(*transform)[1][2]*normal.z,
				(*transform)[2][0]*normal.x+(*transform)[2][1]*normal.y+(*transform)[2][2]*normal.z};
			normal=rotated.Normalized();
			if (SeamGeometry::Dot(cross,cross)<1e-8f) return;
		}
		face.normal={normal.x,normal.y,normal.z};
		if (!SeamGeometry::Valid(face)) return;
		const float distance=centre.DistToSqr(centre.Clamp(low,high));
		int slot=count;
		if (count==capacity)
		{
			slot=0;
			for (int i=1;i<count;++i) if (distances[i]>distances[slot]) slot=i;
			if (distance>=distances[slot]) return;
		}
		else ++count;
		output[slot]=face;distances[slot]=distance;
	};
	if (I::BSPData && I::BSPData->numbrushes>0
		&& m_vBrushStorage.size()>=size_t(I::BSPData->numbrushes))
	{
		CUtlVector<int> brushes(m_vBrushStorage.data(),int(m_vBrushStorage.size()));
		I::EngineTrace->GetBrushesInAABB(mins,maxs,&brushes,MASK_SOLID);
		for (int i=0;i<brushes.Count();++i)
		{
			if (brushes[i]<0 || brushes[i]>=I::BSPData->numbrushes) continue;
			auto it=m_mFaceCache.find(&I::BSPData->map_brushes[brushes[i]]);
			if (it==m_mFaceCache.end()) continue;
			for (const auto& face:it->second) add(face.m_vVertices.data(),int(face.m_vVertices.size()),face.m_vNormal);
		}
	}
	if (I::DispCollTrees && *I::DispCollTrees && I::DispCollTreeCount)
	{
		for (int i=0;i<*I::DispCollTreeCount;++i)
		{
			auto& tree=(*I::DispCollTrees)[i];
			if (!Math::IsBoxIntersectingBox(mins,maxs,tree.m_mins,tree.m_maxs)) continue;
			for (int j=0;j<tree.GetFaceCount();++j)
			{
				auto& tri=tree.m_aTris[j];
				const Vec3 vertices[]={tree.m_aVerts[tri.GetVert(0)],tree.m_aVerts[tri.GetVert(1)],tree.m_aVerts[tri.GetVert(2)]};
				add(vertices,3,tri.m_vecNormal);
			}
		}
	}
	class Enumerator final:public IPartitionEnumerator
	{
	public:
		std::array<IHandleEntity*,256> entries{};int count=0;
		IterationRetval_t EnumElement(IHandleEntity* entity) override
		{
			if (count<int(entries.size())) entries[count++]=entity;
			return ITERATION_CONTINUE;
		}
	} entities;
	if (I::SpatialPartition)
		I::SpatialPartition->EnumerateElementsInBox(I::EngineTrace->SpatialPartitionMask(),mins,maxs,false,&entities);
	for (int i=0;i<entities.count;++i)
	{
		auto* entity=entities.entries[i];if (!entity) continue;
		const bool prop=I::StaticPropMgr->IsStaticProp(entity);
		auto* base=prop?nullptr:reinterpret_cast<CBaseEntity*>(entity);
		if (base && (base->IsPlayer() || !base->GetAbsVelocity().IsZero())) continue;
		auto* collide=prop?I::StaticPropMgr->GetStaticProp(entity):base->GetCollideable();
		if (!collide || !collide->IsSolid()) continue;
		auto it=m_mFaceCache.find(I::ModelInfoClient->GetVCollide(collide->GetCollisionModel()));
		if (it==m_mFaceCache.end()) continue;
		for (const auto& face:it->second)
			add(face.m_vVertices.data(),int(face.m_vVertices.size()),face.m_vNormal,&collide->CollisionToWorldTransform());
	}
	return count;
}

#ifdef WORLD_DEBUG
void CWorld::DrawFace(const Face_t& tFace, int iFlags, std::optional<Color_t> tColor)
{
	if (!tColor)
	{
		switch (tFace.m_iType)
		{
		case FaceTypeEnum::BoxBrush: tColor = Vars::World::BoxBrush.Value; break;
		case FaceTypeEnum::PlaneBrush: tColor = Vars::World::PlaneBrush.Value; break;
		case FaceTypeEnum::Displacement: tColor = Vars::World::Displacement.Value; break;
		case FaceTypeEnum::Prop: tColor = Vars::World::Prop.Value; break;
		case FaceTypeEnum::Entity: tColor = Vars::World::Entity.Value; break;
		default: tColor = { 255, 255, 255 }; break;
		}
	}

	if (iFlags & (DrawTypeEnum::Points | DrawTypeEnum::Edges))
	{
		for (int i = 0; i < tFace.m_vVertices.size(); i++)
		{
			const Vec3& vVertex1 = tFace.m_vVertices[i], &vVertex2 = tFace.m_vVertices[(i + 1) % tFace.m_vVertices.size()];

			if (iFlags & DrawTypeEnum::Points)
				G::BoxStorage.emplace_back(vVertex1, Vec3::Get(-1), Vec3::Get(1), Vec3(0, 0, 0), I::GlobalVars->curtime + 60.f, *tColor, Color_t(0, 0, 0, 0), true);
			if (iFlags & DrawTypeEnum::Edges)
				G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vVertex1, vVertex2), I::GlobalVars->curtime + 60.f, *tColor, true);
		}
	}

	if (iFlags & DrawTypeEnum::Faces)
	{
		for (int i = 0; ++i < tFace.m_vVertices.size() - 1;)
		{
			const Vec3& vVertex1 = tFace.m_vVertices[0], &vVertex2 = tFace.m_vVertices[i], &vVertex3 = tFace.m_vVertices[i + 1];
			G::TriangleStorage.emplace_back(std::array<Vec3, 3>({ vVertex1, vVertex2, vVertex3 }), I::GlobalVars->curtime + 60.f, tColor->Alpha(50), true);
		}
	}

	if (iFlags & DrawTypeEnum::Normals)
	{
		G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(tFace.m_vVertices[0], tFace.m_vVertices[0] + tFace.m_vNormal * 10), I::GlobalVars->curtime + 60.f, tColor->Inverse(), true);
		G::BoxStorage.emplace_back(tFace.m_vVertices[0] + tFace.m_vNormal * 10, Vec3::Get(-1), Vec3::Get(1), Vec3(), I::GlobalVars->curtime + 60.f, tColor->Inverse(), Color_t(0, 0, 0, 0), true);
	}
}
#endif
