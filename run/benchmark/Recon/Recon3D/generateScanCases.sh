#!/bin/bash

#appList=(reconstructError)
#reconSchemeList=(isoInverseDistance RDFPoints gradAlphaSmoothed gradAlpha RDF perfectRDFPoints)
reconSchemeList=( isoRDF plicRDF isoAlpha) # isoRDFIter isoAlpha)
#appList=(isoAdvector isoAdvectorRDF isoAdvectorPerfectRDF isoAdvectorRDFITER)
schemeList=(reconstructError reconstructError)
#schemeList=(isoAdvector isoAdvectorRDF isoAdvectorPerfectRDF isoAdvectorRDFITER)
meshList=( tri hex poly) #hex poly
#CoList=(0)

#RDF


application=reconstructError
curDir=$PWD

#Location of tri meshes
triMeshDir=../triMeshes

for nn in ${!meshList[*]}
do
	meshType=${meshList[$nn]}
	for mm in ${!reconSchemeList[*]}
	do

		scheme=${schemeList[$mm]}
		reconScheme=${reconSchemeList[$mm]}

		#Case location
		series=$PWD/$reconScheme/$meshType


		    #NzList=(5 10 15 30 60 120 240)
	    if [ "$meshType" = "poly" ];
	    then
		    NzList=( 0.05 0.025 0.0125 0.00625 0.0045)
	    elif [ "$meshType" = "tri" ];
	    then
            NzList=( 15 30 60 120 )
	    else
		
		    NzList=( 15 30 60 120 240)
	    fi


		Uz=0.0

		

		mkdir --parents $series

		for n in ${!NzList[*]} 
		do
			#for m in ${!CoList[*]}
			#do
				#Co=${CoList[$m]}
				caseName=N${NzList[$n]} #Co${Co}
				caseDir=$series/$caseName
				echo $caseDir
				cp -r baseCase $caseDir
				
				#./ofset maxAlphaCo "$Co" $caseDir/system/controlDict
				./ofset application "$application" $caseDir/system/controlDict
				sed -i "s/RECONSCHEME/$reconScheme/g" $caseDir/system/fvSolution
				#./ofset Uz "$Uz" $caseDir/0.org/U

				#Generating mesh
				mkdir $caseDir/logs
				cp -r $caseDir/0.org $caseDir/0
				touch $caseDir/case.foam

				if [ "$meshType" = "hex" ];
				then
					nx=${NzList[$n]}
					nz=${NzList[$n]}

					sed  -i "s/replaceNx/$nx/" $caseDir/hexBlock.geo
					sed  -i "s/replaceNz/$nz/" $caseDir/hexBlock.geo

					cd $caseDir
					gmsh -3 hexBlock.geo > $caseDir/logs/gmsh.log 2>&1
					gmshToFoam hexBlock.msh > $caseDir/logs/gmshToFoam.log 2>&1
					changeDictionary > $caseDir/logs/changeDictionary.log 2>&1
					cd $curDir
					#cp -r $caseDir/0.org $caseDir/0
					setAlphaField -case $caseDir > $caseDir/logs/perfectVOF.log 2>&1
                elif [ "$meshType" = "poly" ]
                then
				    nx=${NzList[$n]}
				    nz=${NzList[$n]}

				    sed  -i "s/replaceMaxSize/$nz/" $caseDir/system/meshDict

				    cd $caseDir
					    pMesh  > $caseDir/logs/pMesh.log 2>&1
				    cd $curDir
				else
					#cp $triMeshDir/N${NzList[$n]}/* $caseDir/constant/polyMesh/
					nx=${NzList[$n]}
					nz=${NzList[$n]}
					#nz=${NzList[$n]}
					./ofset2 replaceNx "$nx" $caseDir/triBlock.geo
					./ofset2 replaceNz "$nz" $caseDir/triBlock.geo
					cd $caseDir
					gmsh -3 -optimize_netgen -nt 16 triBlock.geo  > $caseDir/logs/gmsh.log 2>&1
					gmshToFoam triBlock.msh > $caseDir/logs/gmshToFoam.log 2>&1
					changeDictionary > $caseDir/logs/changeDictionary.log 2>&1
					if [ "$meshType" = "poly" ];
					then
			                        #mkdir $caseDir/logs
			                        #Convert from tet to poly mesh
			                        polyDualMesh -case $caseDir -splitAllFaces -overwrite 160 > $caseDir/logs/polyDualMesh.log 2>&1
			                        rm -rf *.obj		
					fi
					#setAlphaField -case $caseDir > $caseDir/logs/perfectVOF.log 2>&1

					cd $curDir
				fi
				
			#done
		done
	done
done


