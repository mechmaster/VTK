/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkGenericClip.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkGenericClip.h"

#include "vtkCellArray.h"
#include "vtkCellData.h"
#include "vtkDoubleArray.h"
#include "vtkGenericCell.h"
#include "vtkImageData.h"
#include "vtkImplicitFunction.h"
#include "vtkIdTypeArray.h"
#include "vtkMergePoints.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkUnsignedCharArray.h"
#include "vtkUnstructuredGrid.h"
#include "vtkGenericDataSet.h"
#include "vtkGenericCellIterator.h"
#include "vtkGenericAdaptorCell.h"
#include "vtkGenericAttribute.h"
#include "vtkGenericAttributeCollection.h"
#include "vtkGenericPointIterator.h"

#include <math.h>

vtkCxxRevisionMacro(vtkGenericClip, "1.3");
vtkStandardNewMacro(vtkGenericClip);
vtkCxxSetObjectMacro(vtkGenericClip,ClipFunction,vtkImplicitFunction);

//----------------------------------------------------------------------------
// Construct with user-specified implicit function; InsideOut turned off; value
// set to 0.0; and generate clip scalars turned off.
vtkGenericClip::vtkGenericClip(vtkImplicitFunction *cf)
{
  this->ClipFunction = cf;
  this->InsideOut = 0;
  this->Locator = NULL;
  this->Value = 0.0;
  this->GenerateClipScalars = 0;

  this->GenerateClippedOutput = 0;
  this->MergeTolerance = 0.01;

  this->vtkSource::SetNthOutput(1,vtkUnstructuredGrid::New());
  this->Outputs[1]->Delete();
  this->InputScalarsSelection = NULL;
  
  this->internalPD=vtkPointData::New();
  this->secondaryPD=vtkPointData::New();
  this->secondaryCD=vtkCellData::New();
}

//----------------------------------------------------------------------------
vtkGenericClip::~vtkGenericClip()
{
  if ( this->Locator )
    {
    this->Locator->UnRegister(this);
    this->Locator = NULL;
    }
  this->SetClipFunction(NULL);
  this->SetInputScalarsSelection(NULL);
  this->internalPD->Delete();
  this->secondaryPD->Delete();
  this->secondaryCD->Delete();
}

//----------------------------------------------------------------------------
// Do not say we have two outputs unless we are generating the clipped output. 
int vtkGenericClip::GetNumberOfOutputs()
{
  if (this->GenerateClippedOutput)
    {
    return 2;
    }
  return 1;
}

//----------------------------------------------------------------------------
// Overload standard modified time function. If Clip functions is modified,
// then this object is modified as well.
unsigned long vtkGenericClip::GetMTime()
{
  unsigned long mTime=this->vtkGenericDataSetToUnstructuredGridFilter::GetMTime();
  unsigned long time;

  if ( this->ClipFunction != NULL )
    {
    time = this->ClipFunction->GetMTime();
    mTime = ( time > mTime ? time : mTime );
    }
  if ( this->Locator != NULL )
    {
    time = this->Locator->GetMTime();
    mTime = ( time > mTime ? time : mTime );
    }

  return mTime;
}

//----------------------------------------------------------------------------
vtkUnstructuredGrid *vtkGenericClip::GetClippedOutput()
{
  if (this->NumberOfOutputs < 2)
    {
    return NULL;
    }
  
  return static_cast<vtkUnstructuredGrid *>(this->Outputs[1]);
}


//----------------------------------------------------------------------------
//
// Clip through data generating surface.
//
void vtkGenericClip::Execute()
{
  vtkGenericDataSet *input=this->GetInput();
  if(input==0)
    {
    return;
    }
  
  vtkUnstructuredGrid *output = this->GetOutput();
  vtkUnstructuredGrid *clippedOutput = this->GetClippedOutput();
  
  vtkIdType numPts = input->GetNumberOfPoints();
  vtkIdType numCells = input->GetNumberOfCells();
  vtkPointData *outPD = output->GetPointData();
  vtkCellData *outCD[2];
  vtkIdType npts;
  vtkIdType *pts;
  int cellType = 0;
  int j;
  vtkIdType estimatedSize;
  vtkUnsignedCharArray *types[2];
  vtkIdTypeArray *locs[2];
  int numOutputs = 1;
  vtkGenericAdaptorCell *cell;

  vtkDebugMacro(<< "Clipping dataset");

  // Initialize self; create output objects
  //
  if ( numPts < 1 )
    {
    vtkErrorMacro(<<"No data to clip");
    return;
    }

  if ( !this->ClipFunction && this->GenerateClipScalars )
    {
    vtkErrorMacro(<<"Cannot generate clip scalars if no clip function defined");
    return;
    }

  // allocate the output and associated helper classes
  estimatedSize = numCells;
  estimatedSize = estimatedSize / 1024 * 1024; //multiple of 1024
  if (estimatedSize < 1024)
    {
    estimatedSize = 1024;
    }
  
  vtkPoints *newPoints = vtkPoints::New();
  newPoints->Allocate(numPts,numPts/2);
  
  vtkCellArray *conn[2];
  conn[0] = vtkCellArray::New();
  conn[0]->Allocate(estimatedSize,estimatedSize/2);
  conn[0]->InitTraversal();
  types[0] = vtkUnsignedCharArray::New();
  types[0]->Allocate(estimatedSize,estimatedSize/2);
  locs[0] = vtkIdTypeArray::New();
  locs[0]->Allocate(estimatedSize,estimatedSize/2);
  
  if(this->GenerateClippedOutput)
    {
    numOutputs = 2;
    conn[1] = vtkCellArray::New();
    conn[1]->Allocate(estimatedSize,estimatedSize/2);
    conn[1]->InitTraversal();
    types[1] = vtkUnsignedCharArray::New();
    types[1]->Allocate(estimatedSize,estimatedSize/2);
    locs[1] = vtkIdTypeArray::New();
    locs[1]->Allocate(estimatedSize,estimatedSize/2);
    }
  
  // locator used to merge potentially duplicate points
  if ( this->Locator == NULL )
    {
    this->CreateDefaultLocator();
    }
  this->Locator->InitPointInsertion (newPoints, input->GetBounds());

  // prepare the output attributes
  vtkGenericAttributeCollection *attributes=input->GetAttributes();
  vtkGenericAttribute *attribute;
  vtkDataArray *attributeArray;
  
  int c=attributes->GetNumberOfAttributes();
  vtkDataSetAttributes *secondaryAttributes;

  int attributeType;
  
  vtkIdType i=0;
  while(i<c)
    {
    attribute=attributes->GetAttribute(i);
    attributeType=attribute->GetType();
    if(attribute->GetCentering()==vtkPointCentered)
      {
      secondaryAttributes=this->secondaryPD;
      
      attributeArray=vtkDataArray::CreateDataArray(attribute->GetComponentType());
      attributeArray->SetNumberOfComponents(attribute->GetNumberOfComponents());
      attributeArray->SetName(attribute->GetName());
      this->internalPD->AddArray(attributeArray);
      attributeArray->Delete();
      if(this->internalPD->GetAttribute(attributeType)==0)
        {
        this->internalPD->SetActiveAttribute(this->internalPD->GetNumberOfArrays()-1,attributeType);
        }
      }
    else // vtkCellCentered
      {
      secondaryAttributes=this->secondaryCD;
      }
    
    attributeArray=vtkDataArray::CreateDataArray(attribute->GetComponentType());
    attributeArray->SetNumberOfComponents(attribute->GetNumberOfComponents());
    attributeArray->SetName(attribute->GetName());
    secondaryAttributes->AddArray(attributeArray);
    attributeArray->Delete();
    
    if(secondaryAttributes->GetAttribute(attributeType)==0)
      {
      secondaryAttributes->SetActiveAttribute(secondaryAttributes->GetNumberOfArrays()-1,
                                              attributeType);
      }
    ++i;
    }
  outPD->InterpolateAllocate(this->secondaryPD,estimatedSize,estimatedSize/2);
  
  outCD[0] = output->GetCellData();
  outCD[0]->CopyAllocate(this->secondaryCD,estimatedSize,estimatedSize/2);
  if ( this->GenerateClippedOutput )
    {
    outCD[1] = clippedOutput->GetCellData();
    outCD[1]->CopyAllocate(this->secondaryCD,estimatedSize,estimatedSize/2);
    }
  
  //vtkGenericPointIterator *pointIt = input->GetPoints();
  vtkGenericCellIterator *cellIt = input->NewCellIterator();  //explicit cell could be 2D or 3D
  
  //Process all cells and clip each in turn
  //
  int abort=0;
  vtkIdType updateTime = numCells/20 + 1;  // update roughly every 5%

  int num[2]; num[0]=num[1]=0;
  int numNew[2]; numNew[0]=numNew[1]=0;
  vtkIdType cellId;

  for (cellId = 0, cellIt->Begin(); !cellIt->IsAtEnd() && !abort; cellId++, cellIt->Next())
    {
    cell = cellIt->GetCell();
    if ( !(cellId % updateTime) )
      {
      this->UpdateProgress((double)cellId / numCells);
      abort = this->GetAbortExecute();
      }

    // perform the clipping
    
    cell->Clip(this->Value, this->ClipFunction, input->GetAttributes(),
               input->GetTessellator(),this->InsideOut,this->Locator,conn[0],
               outPD,outCD[0],this->internalPD,this->secondaryPD,
               this->secondaryCD);
    numNew[0] = conn[0]->GetNumberOfCells() - num[0];
    num[0] = conn[0]->GetNumberOfCells();
 
    if ( this->GenerateClippedOutput )
      {
      cell->Clip(this->Value, this->ClipFunction, input->GetAttributes(),
                 input->GetTessellator(),this->InsideOut,this->Locator,conn[1],
                 outPD,outCD[1],this->internalPD,this->secondaryPD,
                 this->secondaryCD);
      
      numNew[1] = conn[1]->GetNumberOfCells() - num[1];
      num[1] = conn[1]->GetNumberOfCells();
      }

    for (i=0; i<numOutputs; i++) //for both outputs
      {
      for (j=0; j < numNew[i]; j++) 
        {
        locs[i]->InsertNextValue(conn[i]->GetTraversalLocation());
        conn[i]->GetNextCell(npts,pts);
        
        //For each new cell added, got to set the type of the cell
        switch ( cell->GetDimension() )
          {
          case 0: //points are generated--------------------------------
            cellType = (npts > 1 ? VTK_POLY_VERTEX : VTK_VERTEX);
            break;

          case 1: //lines are generated---------------------------------
            cellType = (npts > 2 ? VTK_POLY_LINE : VTK_LINE);
            break;

          case 2: //polygons are generated------------------------------
            cellType = (npts == 3 ? VTK_TRIANGLE : 
                        (npts == 4 ? VTK_QUAD : VTK_POLYGON));
            break;

          case 3: //tetrahedra or wedges are generated------------------
            cellType = (npts == 4 ? VTK_TETRA : VTK_WEDGE);
            break;
          } //switch

        types[i]->InsertNextValue(cellType);
        } //for each new cell
      } //for both outputs
    } //for each cell
  cellIt->Delete();
  
  output->SetPoints(newPoints);
  output->SetCells(types[0], locs[0], conn[0]);
  conn[0]->Delete();
  types[0]->Delete();
  locs[0]->Delete();

  if ( this->GenerateClippedOutput )
    {
    clippedOutput->SetPoints(newPoints);
    clippedOutput->SetCells(types[1], locs[1], conn[1]);
    conn[1]->Delete();
    types[1]->Delete();
    locs[1]->Delete();
    }
  
  newPoints->Delete();
  this->Locator->Initialize();//release any extra memory
  output->Squeeze();
}

//----------------------------------------------------------------------------
// Specify a spatial locator for merging points. By default, 
// an instance of vtkMergePoints is used.
void vtkGenericClip::SetLocator(vtkPointLocator *locator)
{
  if ( this->Locator == locator)
    {
    return;
    }
  
  if ( this->Locator )
    {
    this->Locator->UnRegister(this);
    this->Locator = NULL;
    }

  if ( locator )
    {
    locator->Register(this);
    }

  this->Locator = locator;
  this->Modified();
}

//----------------------------------------------------------------------------
void vtkGenericClip::CreateDefaultLocator()
{
  if ( this->Locator == NULL )
    {
    this->Locator = vtkMergePoints::New();
    this->Locator->Register(this);
    this->Locator->Delete();
    }
}

//----------------------------------------------------------------------------
void vtkGenericClip::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);

  os << indent << "Merge Tolerance: " << this->MergeTolerance << "\n";
  if ( this->ClipFunction )
    {
    os << indent << "Clip Function: " << this->ClipFunction << "\n";
    }
  else
    {
    os << indent << "Clip Function: (none)\n";
    }
  os << indent << "InsideOut: " << (this->InsideOut ? "On\n" : "Off\n");
  os << indent << "Value: " << this->Value << "\n";
  if ( this->Locator )
    {
    os << indent << "Locator: " << this->Locator << "\n";
    }
  else
    {
    os << indent << "Locator: (none)\n";
    }

  os << indent << "Generate Clip Scalars: " 
     << (this->GenerateClipScalars ? "On\n" : "Off\n");

  os << indent << "Generate Clipped Output: " 
     << (this->GenerateClippedOutput ? "On\n" : "Off\n");

  if (this->InputScalarsSelection)
    {
    os << indent << "InputScalarsSelection: " 
       << this->InputScalarsSelection << endl;
    }
}
