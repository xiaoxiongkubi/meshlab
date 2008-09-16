/****************************************************************************
* VCGLib                                                            o o     *
* Visual and Computer Graphics Library                            o     o   *
*                                                                _   O  _   *
* Copyright(C) 2004                                                \/)\/    *
* Visual Computing Lab                                            /\/|      *
* ISTI - Italian National Research Council                           |      *
*                                                                    \      *
* All rights reserved.                                                      *
*                                                                           *
* This program is free software; you can redistribute it and/or modify      *   
* it under the terms of the GNU General Public License as published by      *
* the Free Software Foundation; either version 2 of the License, or         *
* (at your option) any later version.                                       *
*                                                                           *
* This program is distributed in the hope that it will be useful,           *
* but WITHOUT ANY WARRANTY; without even the implied warranty of            *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
* GNU General Public License (http://www.gnu.org/licenses/gpl.txt)          *
* for more details.                                                         *
*                                                                           *
****************************************************************************/

#ifndef HOLE_H
#define HOLE_H

#include <utility>
#include <vector>
#include <float.h>
#include <GL/glew.h>
#include "vcg/simplex/face/pos.h"
#include "vcg/space/point3.h"
#include "vcg/complex/trimesh/hole.h"
#include "vcg/complex/trimesh/allocate.h"
#include <vcg/complex/trimesh/closest.h>
#include <vcg/space/index/grid_static_ptr.h>
#include "vcg/space/color4.h"
#include <meshlab/meshmodel.h>

/** An hole type
 */
template <class MESH>
class FgtHole : public vcg::tri::Hole<MESH>::Info
{
public:

	enum FillerMode
	{
		Trivial, MinimumWeight, SelfIntersection
	};

	typedef typename MESH::FaceType					FaceType;
	typedef typename MESH::FacePointer				FacePointer;
	typedef typename std::vector<FacePointer>		FacePointerVector;
	typedef typename MESH::FaceIterator				FaceIterator;
    typedef typename MESH::CoordType				CoordType;
	typedef typename MESH::VertexType				VertexType;
	typedef typename MESH::ScalarType				ScalarType;
	typedef typename vcg::face::Pos<FaceType>		PosType;
	typedef typename std::vector<PosType>			PosVector;
	typedef typename PosVector::iterator			PosIterator;
	typedef typename vcg::tri::Hole<MESH>			vcgHole;
	typedef typename vcgHole::Info					HoleInfo;
	typedef typename std::vector< FgtHole<MESH> >	HoleVector;
	typedef typename HoleVector::iterator			HoleIterator;
	

	FgtHole(HoleInfo &hi, QString holeName) : 
		HoleInfo(hi.p, hi.size, hi.bb)
	{
		name = holeName;
		filled = false;
		comp = false;
		accepted = true;
		selected = false;
		isBridged=false;
		perimeter = HoleInfo::Perimeter();
		findNonManifoldness();
	};

	FgtHole(PosType startPos, QString holeName)
	{
		assert(startPos.IsBorder());
		name = holeName;
		filled = false;
		comp = false;
		accepted = true;
		selected = false;
		isBridged=false;
		this->p = startPos;
		updateInfo();		
	};


	~FgtHole() {};

	inline int Size() const { return this->size; };
	inline ScalarType Perimeter() const	{ return this->perimeter; };
	inline bool IsFilled() const { return this->filled; };
	inline bool IsSelected() const { return this->selected; };
	inline void SetSelect(bool value) { selected = value;	};
	inline bool IsCompenetrating() const { return filled && comp; };
	inline bool IsAccepted() const { return !filled || accepted; };
	inline void SetAccepted(bool val) { accepted = val; };
	inline bool IsNonManifold() const { return isNonManifold; };
	inline bool IsBridged() const { return isBridged; };
	inline void SetBridged(bool val){ isBridged=val; };

	inline void SetStartPos(PosType initP)
	{
		assert(!IsFilled());
		assert(initP.IsBorder());
		this->p = initP;
		updateInfo();
	};

	void Draw() const
	{
		typename std::vector<VertexType*>::const_iterator it = vertexes.begin();
		glBegin(GL_LINE_LOOP);
		for( ; it != vertexes.end(); it++)
			glVertex( (*it)->P() );
		glEnd();
	};

	void DrawCompenetratingFace(GLenum glmode) const
	{
		assert(IsCompenetrating());
		
		std::vector<FacePointer> patch;
		getPatchFaces(patch);
		typename std::vector<FacePointer>::const_iterator it;
		
		glBegin(glmode);
		for( it=patch.begin(); it != patch.end(); it++)
			if((*it)->IsUserBit(PatchCompFlag))
			{
				glVertex( (*it)->V(0)->P() );
				glVertex( (*it)->V(1)->P() );
				glVertex( (*it)->V(2)->P() );
			}
		
		glEnd();
	}

	/*  Reset flag used by this plugin to mark this hole and its patch. 
	 *  Bridges face can finded along the hole and it needs look at adjacent face because
	 *  a bridge could be build from other bridge face.
	 */
	void ResetFlag()
	{
		std::vector<FacePointer> bridgesFaces;
		if(filled)
		{
			std::vector<FacePointer> patch;
			getPatchFaces(patch);
			typename std::vector<FacePointer>::iterator it;
			for( it=patch.begin(); it != patch.end(); it++)
			{
				(*it)->ClearUserBit(HolePatchFlag);
				(*it)->ClearUserBit(PatchCompFlag);
				for(int i=0; i<3; i++)
				{
					FacePointer adjF = (*it)->FFp(i);
					adjF->ClearUserBit(HoleFlag);
					if(IsBridgeFace(*adjF))
						bridgesFaces.push_back(adjF);
				}
			}
		}
		else
		{
			// we can walk the border to find hole's faces
			PosType curPos = p;
			do{
				curPos.f->ClearUserBit(HoleFlag);
				if( IsBridgeFace(*curPos.f))
					bridgesFaces.push_back(curPos.f);
				curPos.NextB();
			}while( curPos != p );
		}

		while(bridgesFaces.size()>0)
		{
			FacePointer f = bridgesFaces.back();
			bridgesFaces.pop_back();
			f->ClearUserBit(BridgeFlag);
			for(int i=0; i<3; i++)
			{
				FacePointer adjF = f->FFp(i);
				if(IsBridgeFace(*adjF) && !IsHoleBorderFace(*adjF))
					bridgesFaces.push_back(adjF);
			}
		}

	};


	/* Restore hole, remove patch faces applied to mesh to fill this hole*/
	void RestoreHole(MESH &mesh)
	{
		assert(filled);
		std::vector<FaceType*> patches;
		getPatchFaces(patches);
		filled = false;
		
		typename std::vector<FaceType*>::iterator it;
		for(it = patches.begin(); it!=patches.end(); it++)
		{
			assert(IsPatchFace(**it));
			if(!(**it).IsD())
				vcg::tri::Allocator<MESH>::DeleteFace(mesh, **it);
		}		
	};

	void Fill(FillerMode mode, MESH &mesh, std::vector<FacePointer *> &local_facePointer)
	{
		switch(mode)
		{
		case FgtHole<MESH>::Trivial:
			vcgHole::FillHoleEar<vcg::tri::TrivialEar<MESH> >(mesh, *this, HolePatchFlag, local_facePointer);
			break;
		case FgtHole<MESH>::MinimumWeight:
			vcgHole::FillHoleEar<vcg::tri::MinimumWeightEar<MESH> >(mesh, *this, HolePatchFlag, local_facePointer);
			break;
		case FgtHole<MESH>::SelfIntersection:
			vcgHole::FillHoleEar<tri::SelfIntersectionEar<MESH> >(mesh, *this, HolePatchFlag, local_facePointer);
			break;
		}

		filled = true;
		accepted = true;
		comp = false;
		updatePatchState(mesh);		
	};

	
private:

	/*  Walking the hole computing vcgHole::Info data */
	void updateInfo()
	{
		assert(!IsFilled());
		vertexes.clear();		
		isNonManifold = false;
		bb.SetNull();
		size = 0;

		PosType curPos = p;		
		do{
			assert(!curPos.f->IsD());
			curPos.f->SetUserBit(HoleFlag);
			bb.Add(curPos.v->cP());
			++size;
			vertexes.push_back(curPos.v);
			if(curPos.v->IsV())
				isNonManifold = true;
			else
				curPos.v->SetV();
			
			curPos.NextB();
			assert(curPos.IsBorder());
		}while( curPos != p );
		
		curPos = p;
		do{
			curPos.v->ClearV();			
			curPos.NextB();
		}while( curPos != p );

		perimeter = HoleInfo::Perimeter();
	};

	/*  Walking the hole storing border pos and finding non manifold vertex */
	void findNonManifoldness()
	{
		assert(!IsFilled());
		isNonManifold = false;
		PosType curPos = p;
		do{
			vertexes.push_back(curPos.v);
			if(curPos.v->IsV())
				isNonManifold = true;
			else
				curPos.v->SetV();			
			curPos.NextB();
		}while( curPos != p );
		
		curPos = p;
		do{
			curPos.v->ClearV();			
			curPos.NextB();
		}while( curPos != p );
	};

	/* test its auto compenetration	*/
	void updatePatchState(MESH &mesh)
	{
		assert(filled);
		comp = false;
		vcg::GridStaticPtr<FaceType, ScalarType > gM;
		gM.Set(mesh.face.begin(),mesh.face.end());

		std::vector<FaceType*> inBox;
		vcg::Box3< ScalarType> bbox;
		FacePointerVector patches;
		getPatchFaces(patches);

		FacePointerVector::iterator pi = patches.begin();
		for( ; pi!=patches.end(); ++pi)
		{
			//assert(!IsHoleBorderFace(*pbi->f));//*****
			FacePointer f = *pi;
			f->SetUserBit(HolePatchFlag);

			// prendo le facce che intersecano il bounding box di *fi
			f->GetBBox(bbox);
			vcg::trimesh::GetInBoxFace(mesh, gM, bbox,inBox);

			typename std::vector<FaceType*>::iterator fib;
			for(fib=inBox.begin();fib!=inBox.end();++fib)
			{
				// tra le facce che hanno i boundingbox intersecanti non considero come compenetranti
				//    - la faccia corrispondente della mesh a quella della patch
				//    - facce che condividono un edge o anche un vertice
				
				bool adj=false;

				for (int i=0; i<3 && !adj; i++)
					for (int j=0;j<3;j++)
						if (f->V(i) == (*fib)->V(j))
						{
							adj = true;
							break;							
						}
					
				if(!adj)
					if( vcg::Intersection<FaceType>(*f, **fib ))
					{
						comp = true;
						f->SetUserBit(PatchCompFlag);
						continue;
					}			
			} // for inbox...
			inBox.clear();
		}
	};

	void getPatchFaces(std::vector<FacePointer> &patches) const
	{
		assert(filled);
		patches.clear();		
		std::vector<FacePointer> stack;
		PosType pos = p;
		pos.FlipF();
		assert(IsPatchFace(*pos.f));  //rimuovere
		pos.f->SetV();
		stack.push_back(pos.f);
		while(stack.size()>0)
		{
			FacePointer f = stack.back();
			stack.pop_back();
			patches.push_back(f);

			//visito le facce patch adiacenti ai vertici di questa faccia patch
			for(int v=0; v<3; v++)
			{
				pos = PosType(f, v);
				do{
					pos.FlipF();
					pos.FlipE();
					if(IsPatchFace(*pos.f) && !pos.f->IsV())
					{
						pos.f->SetV();
						stack.push_back(pos.f);
					}
				}while(pos.f != f);
			}
		}

		typename std::vector<FacePointer>::iterator it;
		for(it=patches.begin(); it!=patches.end(); ++it)
			(*it)->ClearV();
	};
	
	/* Check if face is a border face of this hole */
	bool haveBorderFace(FacePointer bFace) const
	{
		// per essere una faccia del bordo di questo hole deve avere almeno 2 vertici 
		// conenuti nella lista di vertici del buco
		assert(IsHoleBorderFace(*bFace));
		assert(!filled);

		PosType curPos = p;
		do{
			if(curPos.f == bFace)
				return true;
			curPos.NextB();
		}while( curPos != p );
		return false;
	}

	/* Check if pFace is a patch face of this hole */
	bool havePatchFace(FacePointer pFace) const
	{
		// essendo chiuso con Ear ogni faccia patch ha in comune al suo buco almeno un vertice
		assert( IsPatchFace(*pFace) );
		assert(filled);
		typename std::vector<VertexType*>::const_iterator it;
		for(it = vertexes.begin(); it!=vertexes.end(); it++)
			if(pFace->V(0) == *it || pFace->V(1) == *it || pFace->V(2) == *it)
				return true;
		return false;
	};

	/********* Static functions **********/
public:

	static inline bool IsHoleBorderFace(FaceType &face)
	{ return face.IsUserBit(HoleFlag); };

	static inline bool IsPatchFace(FaceType &face)
	{ return face.IsUserBit(HolePatchFlag); };

	static inline bool IsBridgeFace(FaceType &face)
	{ return face.IsUserBit(BridgeFlag); };
	

	/* Inspect a mesh to find its holes. */
	static int GetMeshHoles(MESH &mesh, HoleVector &ret) 
	{
		assert(HoleFlag ==-1);
		ret.clear();
		std::vector<HoleInfo> vhi;
				
		//prendo la lista di info(sugli hole) tutte le facce anche le non selezionate
		HoleFlag = vcgHole::GetInfo(mesh, false, vhi);
		HolePatchFlag = FaceType::NewBitFlag();
		PatchCompFlag = FaceType::NewBitFlag();
		BridgeFlag = FaceType::NewBitFlag();

		typename std::vector<HoleInfo>::iterator itH = vhi.begin();
		int i=0;
		for( ; itH != vhi.end(); itH++)
		{
			ret.push_back(FgtHole<MESH>(*itH, QString("Hole_%1").arg(i,3,10,QChar('0')) ));
			i++;
		}
		return HoleFlag;
	}

	/** Return index of hole adjacent to picked face into holes vector.
	 *  Also return the iterator on correct position.
	 */
	static int FindHoleFromBorderFace(FacePointer bFace, HoleVector &holes, HoleIterator &it) 
	{ 
		assert(IsHoleBorderFace(*bFace));
		int index = 0;
		 HoleIterator hit = holes.begin();
		for( ; hit != holes.end(); ++hit)
		{
			if(!hit->IsFilled())
			{
				if(hit->haveBorderFace(bFace))
				{
					it = hit;
					return index;
				}
			}
			else
			{
				// l'hole Ë riempito, non c'Ë pi˘ il bordo, nextB() non funzionerebbe
				// prendo la faccia patch adiacente e cerco quella, in questo modo non ho bisogno
				int i=0;
				for( ; i<3; i++)
					if(IsPatchFace(*bFace->FFp(i)))
						break;
				if(i<3)
					if(hit->havePatchFace(bFace->FFp(i)))
					{
						it = hit;
						return index;
					}
			}

			index++;
		}
		it = holes.end();	// invalid iterator
		return -1;
	}

	/** Return index into holes vector of hole adjacent to picked face */
	static int FindHoleFromPatchFace(FacePointer bFace, HoleVector &holes, HoleIterator &it)
	{
		assert(bFace->IsUserBit(HolePatchFlag));
		int index = 0;
		typename HoleIterator hit = holes.begin();
		for( ; hit != holes.end(); ++hit)
		{
			// for each hole check if face is its border face
			if(hit->IsFilled())
				if(hit->havePatchFace(bFace))
				{
					it = hit;
					return index;
				}
			index++;
		}
		it = holes.end();	// invalid iterator
		return -1; // means no find hole
	};

	static void DeleteFlag()
	{
		FaceType::DeleteBitFlag(BridgeFlag); BridgeFlag=-1;
		FaceType::DeleteBitFlag(PatchCompFlag); PatchCompFlag=-1;
		FaceType::DeleteBitFlag(HolePatchFlag); HolePatchFlag=-1;
		FaceType::DeleteBitFlag(HoleFlag); HoleFlag=-1;
	}

	static void AddFaceReference(HoleVector& holes, std::vector<FacePointer*> &facesReferences)
	{
		typename HoleVector::iterator it = holes.begin();
		for( ; it!=holes.end(); it++)
			facesReferences.push_back(&it->p.f);
	}

public:
	static int HoleFlag;
	static int HolePatchFlag;
	static int PatchCompFlag;
	static int BridgeFlag;

	QString name;
	
private:
	bool filled;
	bool comp;
	bool accepted;
	bool selected;
	bool isNonManifold;
	bool isBridged;
	ScalarType perimeter;

	std::vector<VertexType*> vertexes;
};

int FgtHole<CMeshO>::HoleFlag = -1;
int FgtHole<CMeshO>::HolePatchFlag = -1;
int FgtHole<CMeshO>::PatchCompFlag = -1;
int FgtHole<CMeshO>::BridgeFlag = -1;

#endif
