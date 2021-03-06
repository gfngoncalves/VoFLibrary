/*---------------------------------------------------------------------------*\
            Copyright (c) 2017-2019, German Aerospace Center (DLR)
-------------------------------------------------------------------------------
License
    This file is part of the VoFLibrary source code library, which is an 
	unofficial extension to OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

//#include "dummyTransform.H"
#include "emptyPolyPatch.H"
#include "reconstructedDistanceFunction.H"
#include "processorPolyPatch.H"
#include "syncTools.H"
#include "wedgePolyPatch.H"
#include "indexedOctree.H"
#include "treeDataPoint.H"

// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

Foam::autoPtr<Foam::indirectPrimitivePatch>
Foam::reconstructedDistanceFunction::coupledFacesPatch() const
{
    const polyBoundaryMesh& patches = mesh_.boundaryMesh();

    label nCoupled = 0;

    for (const polyPatch& pp : patches)
    {
        if (isA<coupledPolyPatch>(pp))
        {
            nCoupled += pp.size();
        }
    }
    labelList nCoupledFaces(nCoupled);
    nCoupled = 0;

    for (const polyPatch& pp : patches)
    {
        if (isA<coupledPolyPatch>(pp))
        {
            label facei = pp.start();

            forAll(pp, i)
            {
                nCoupledFaces[nCoupled++] = facei++;
            }
        }
    }

    return autoPtr<indirectPrimitivePatch>::New
    (
        IndirectList<face>
        (
            mesh_.faces(),
            nCoupledFaces
        ),
        mesh_.points()
    );
}

void Foam::reconstructedDistanceFunction::markCellsNearSurf
(
    const boolList& interfaceCells,
    const label neiRingLevel
)
{
    // performance might be improved by increasing the saving last iteations cells
    // in a Map and loop over the map 
    if (mesh_.topoChanging())
    {
        // Introduced resizing to cope with changing meshes
        if(nextToInterface_.size() != mesh_.nCells())
        {
            nextToInterface_.resize(mesh_.nCells());
        }
        coupledBoundaryPoints_ = coupledFacesPatch()().meshPoints();
    }

    const labelListList& pCells = mesh_.cellPoints();
    const labelListList& cPoints = mesh_.pointCells();

    boolList alreadyMarkedPoint(mesh_.nPoints(),false);
    nextToInterface_ = false;

    // do coupled face first
    Map<bool> syncMap;

    for(int level = 0;level<=neiRingLevel;level++) 
    {
        // parallel
        if(level > 0)
        {
            forAll(coupledBoundaryPoints_,i)
            {
                const label& pi = coupledBoundaryPoints_[i];
                forAll(mesh_.pointCells()[pi], j)
                {
                    const label celli = cPoints[pi][j];
                    if (cellDistLevel_[celli] == level-1)
                    {
                        syncMap.insert(pi, true);
                        break;
                    }
                }
            }
        
            syncTools::syncPointMap(mesh_, syncMap, orEqOp<bool>());

            // mark parallel points first
            forAllConstIter(Map<bool>, syncMap, iter)
            {
                const label pi = iter.key();
                
                if(!alreadyMarkedPoint[pi])
                {
                    // loop over all cells attached to the point
                    forAll(cPoints[pi],j) 
                    {
                        const label& pCelli = cPoints[pi][j];
                        if(cellDistLevel_[pCelli] == -1)
                        {
                            cellDistLevel_[pCelli] = level;
                            nextToInterface_[pCelli] = true;
                        }
                        
                    }
                }
                
                alreadyMarkedPoint[pi] = true;
            }
        }


        forAll(cellDistLevel_, celli)
        {
            if(level == 0)
            {
                if(interfaceCells[celli])
                {
                    cellDistLevel_[celli] = 0;
                    nextToInterface_[celli] = true;
                }
                else
                {
                    cellDistLevel_[celli] = -1;
                }
            }
            else
            {
                if (cellDistLevel_[celli] == level-1)
                {
                    forAll(pCells[celli], i)
                    {
                        const label& pI = pCells[celli][i];
                        // interfacePoint_[pI] = true;
                        if(!alreadyMarkedPoint[pI])
                        {
                            forAll(cPoints[pI],j) // loop over all cells attached to the point
                            {
                                const label& pCelli = cPoints[pI][j];
                                if(cellDistLevel_[pCelli] == -1)
                                {
                                    cellDistLevel_[pCelli] = level;
                                    nextToInterface_[pCelli] = true;
                                }
                                
                            }
                        }
                        alreadyMarkedPoint[pI] = true;
 
                    }
                }
            }
        }

    }

}

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::reconstructedDistanceFunction::reconstructedDistanceFunction
(
    const fvMesh& mesh
)
:
    volScalarField
    (
        IOobject
        (
            "RDF_",
            mesh.time().timeName(),
            mesh,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh,
        dimensionedScalar("reconDistFunc",dimLength,0),
        "calculated"
    ),
    mesh_(mesh),
    coupledBoundaryPoints_(coupledFacesPatch()().meshPoints()),
    cellDistLevel_
    (
          IOobject
          (
              "cellDistLevel_",
              mesh.time().timeName(),
              mesh,
              IOobject::NO_READ,
              IOobject::AUTO_WRITE
          ),
          mesh,
          dimensionedScalar("cellDistLevel_",dimless,-1),
          "calculated"
    ),
    nextToInterface_(mesh.nCells(),false)
{
}

// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

const Foam::volScalarField&  Foam::reconstructedDistanceFunction::constructRDF
(
    const boolList& nextToInterface,
    const volVectorField& centre,
    const volVectorField& normal,
    zoneDistribute& distribute,
    bool updateStencil
)
{
    volScalarField& reconDistFunc = *this;

    if(nextToInterface.size() != centre.size())
    {
        FatalErrorInFunction
        << "size of nextToInterface: " << nextToInterface.size()
        << "size of centre:" <<  centre.size()
        << "do not match. Did the mesh change?"
        << exit(FatalError);
        return reconDistFunc;
    }


    distribute.setUpCommforZone(nextToInterface,updateStencil);

    Map<vector > mapCentres = distribute.getDatafromOtherProc(nextToInterface,centre);
    Map<vector > mapNormal = distribute.getDatafromOtherProc(nextToInterface,normal);

    const labelListList& stencil = distribute.getStencil();


    forAll(nextToInterface,celli)
    {
        if(nextToInterface[celli])
        { 
            if(mag(normal[celli]) != 0) // interface cell
            {
                vector n = -normal[celli]/mag(normal[celli]);
                scalar dist = (centre[celli] - mesh_.C()[celli]) & n;
                reconDistFunc[celli] = dist;
            }
            else // nextToInterfaceCell or level == 1 cell
            {
                scalar averageDist = 0;
                scalar avgWeight = 0;
                const point p = mesh_.C()[celli];

                forAll(stencil[celli],i)
                {
                    const label& gblIdx = stencil[celli][i];
                    vector n = -distribute.getValue(normal,mapNormal,gblIdx); // note the minus
                    if(mag(n) != 0)
                    {
                        n /= mag(n);
                        vector c = distribute.getValue(centre,mapCentres,gblIdx);
                        vector distanceToIntSeg = (c - p);
                        scalar distToSurf = distanceToIntSeg & (n);
                        scalar weight = 0;

                        if (mag(distanceToIntSeg) != 0)
                        {
                            distanceToIntSeg /= mag(distanceToIntSeg);
                            weight = sqr(mag(distanceToIntSeg & n));
                        }
                        else // exactly on the center
                        {
                            weight = 1;
                        }
                        averageDist += distToSurf * weight;
                        avgWeight += weight;
                    }
                }

                if (avgWeight != 0)
                {
                    reconDistFunc[celli] = averageDist / avgWeight;
                }
                
            }
        }
        else
        {
            reconDistFunc[celli] = 0;
        }  

    }

    forAll(reconDistFunc.boundaryField(), patchI)
    {
        if(reconDistFunc.boundaryField().types()[patchI] == "calculated")
        {
            const polyPatch pp = mesh_.boundaryMesh()[patchI];
            fvPatchScalarField& pHeight = reconDistFunc.boundaryFieldRef()[patchI];
            forAll(pHeight, i)
            {
                const label& pCellI = pp.faceCells()[i];

                if(nextToInterface_[pCellI])
                {
                    scalar averageDist = 0;
                    scalar avgWeight = 0;
                    const point p = mesh_.C().boundaryField()[patchI][i];

                    forAll(stencil[pCellI],j)
                    {
                        const label& gblIdx = stencil[pCellI][j];
                        vector n = -distribute.getValue(normal,mapNormal,gblIdx); // note the minus
                        if(mag(n) != 0)
                        {
                            n /= mag(n);
                            vector c = distribute.getValue(centre,mapCentres,gblIdx);
                            vector distanceToIntSeg = (c - p);
                            scalar distToSurf = distanceToIntSeg & (n);
                            scalar weight = 0;

                            if (mag(distanceToIntSeg) != 0)
                            {
                                distanceToIntSeg /= mag(distanceToIntSeg);
                                weight = sqr(mag(distanceToIntSeg & n));
                            }
                            else // exactly on the center
                            {
                                weight = 1;
                            }
                            averageDist += distToSurf * weight;
                            avgWeight += weight;
                        }
                    }

                    if (avgWeight != 0)
                    {
                        pHeight[i] = averageDist / avgWeight;
                    }
                    else
                    {
                        pHeight[i] = 0;
                    } 


                }
                else
                {
                    pHeight[i] = 0;
                } 
            }
        }
    }

    reconDistFunc.correctBoundaryConditions();

    return reconDistFunc;

}

const Foam::volScalarField&  Foam::reconstructedDistanceFunction::constructRDF
(
    const boolList& interfaceCells,
    const volVectorField& centre,
    const volVectorField& normal,
    const label neiRingLevel,
    zoneDistribute& distribute
)
{
    volScalarField& reconDistFunc = *this;

    if(neiRingLevel != 1 && neiRingLevel != 2)
    {
        FatalErrorInFunction
        << "currently the maximum distance to surface is two cells  "
        << "but neiRingLevel is " << neiRingLevel
        << exit(FatalError);
        return reconDistFunc;
    }

    markCellsNearSurf(interfaceCells,1);

    boolList nextToInterface(mesh_.nCells(),false);

    forAll(cellDistLevel_,celli)
    {
        if(cellDistLevel_[celli] >= 0)
        {
            nextToInterface[celli] = true;
        }
    }

    distribute.setUpCommforZone(nextToInterface);
    Map<Field <vector > > mapCC;

    Map<Field <vector > > mapCentres = distribute.getFields(nextToInterface,centre);
    Map<Field <vector > > mapNormal = distribute.getFields(nextToInterface,normal);
    
    if(neiRingLevel == 2)
    {
        mapCC = distribute.getFields(nextToInterface,mesh_.C());
    }

    Map<Field <point > >::const_iterator centreIter = mapCentres.cbegin();
    Map<Field <vector > >::const_iterator normalIter = mapNormal.cbegin();
    Map<Field <vector > >::const_iterator ccIter = mapCC.cbegin();

    const labelListList& stencil = distribute.getStencil();
    const globalIndex& globalNumbering = distribute.globalNumbering();


    while(centreIter != mapCentres.cend())
    {
        const label celli = centreIter.key(); // is in local numbering
        const Field <vector >& centres = centreIter();
        const Field <vector >& normals = normalIter();

        if(mag(normals[0]) != 0) // interface cell
        {
            vector n = -normals[0]/mag(normals[0]);
            scalar dist = (centres[0] - mesh_.C()[celli]) & n;
            reconDistFunc[celli] = dist;
        }
        else // nextToInterfaceCell or level == 1 cell
        {
            scalar averageDist = 0;
            scalar avgWeight = 0;
            const point p = mesh_.C()[celli];

            forAll(centres,i)
            {
                if(mag(normals[i]) != 0)
                {
                    vector n = -normals[i]/mag(normals[i]);
                    vector distanceToIntSeg = (centres[i] - p);
                    scalar distToSurf = distanceToIntSeg & (n);
                    scalar weight = 0;

                    if (mag(distanceToIntSeg) != 0)
                    {
                        distanceToIntSeg /= mag(distanceToIntSeg);
                        weight = sqr(mag(distanceToIntSeg & n));
                    }
                    else // exactly on the center
                    {
                        weight = 1;
                    }
                    averageDist += distToSurf * weight;
                    avgWeight += weight;
                }
            }

            if (avgWeight != 0)
            {
                reconDistFunc[celli] = averageDist / avgWeight;
            }

            if(neiRingLevel == 2)
            {
                const Field <vector >& ccs = ccIter();
                forAll(stencil[celli],i)
                {
                    const label gblIdx = stencil[celli][i];
                    if(globalNumbering.isLocal(gblIdx))
                    {
                        const label idx = globalNumbering.toLocal(gblIdx);
                        if(idx < mesh_.nCells() && !nextToInterface[idx])
                        {
                            reconDistFunc[idx] = GREAT;
                            forAll(centres,j)
                            {
                                if(mag(normals[j]) != 0)
                                {
                                    vector n = -normals[j]/mag(normals[j]);
                                    scalar distToSurf = (centres[j] - ccs[i]) & (n);
                                    if(mag(distToSurf) < mag(reconDistFunc[idx]))
                                    {
                                        reconDistFunc[idx]  = distToSurf;
                                    }
                                    
                                }
                            }
                        }
                    }
                }
            }
        }



        ++centreIter;
        ++normalIter;

        if(neiRingLevel == 2)
        {
            ++ccIter;
        }
    }

    return reconDistFunc;



}


const Foam::volScalarField&  Foam::reconstructedDistanceFunction::constructRDFOctree
(
    const boolList& nextToInterface,
    const pointField& centre,
    const vectorField& normals
)
{
    volScalarField& reconDistFunc = *this;

    Random rndGen(17301893);

    // Slightly extended bb. Slightly off-centred just so on symmetric
    // geometry there are less face/edge aligned items.
    treeBoundBox bb
    (
        treeBoundBox(centre).extend(rndGen, 1e-4)
    );

    bb.min() -= point(ROOTVSMALL, ROOTVSMALL, ROOTVSMALL);
    bb.max() += point(ROOTVSMALL, ROOTVSMALL, ROOTVSMALL);


    indexedOctree<treeDataPoint> surfaceTree
    (
        treeDataPoint
        (
            centre
        ),
        bb,     // bb
        8,      // maxLevel
        10,     // leafsize
        3.0     // duplicity
    );
    
    forAll(reconDistFunc,celli)
    {
        if(nextToInterface[celli])
        { 
            const point& p = mesh_.C()[celli];
            pointIndexHit pHit =  surfaceTree.findNearest (p, GREAT);
            const label idx = pHit.index();
            vector n = -normals[idx];
            n /= mag(n);
            scalar dist = (centre[pHit.index()]-p) & n;
            reconDistFunc[celli] = dist;
        } 
        else
        {
            reconDistFunc[celli] = 0;
        } 
    }

    return reconDistFunc;
}

// ************************************************************************* //
