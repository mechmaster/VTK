/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkInformationInformationKey.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
// .NAME vtkInformationInformationKey - Key for vtkInformation values.
// .SECTION Description
// vtkInformationInformationKey is used to represent keys in vtkInformation
// for other information objects.

#ifndef __vtkInformationInformationKey_h
#define __vtkInformationInformationKey_h

#include "vtkInformationKey.h"

class VTK_COMMON_EXPORT vtkInformationInformationKey : public vtkInformationKey
{
public:
  vtkTypeRevisionMacro(vtkInformationInformationKey,vtkInformationKey);
  void PrintSelf(ostream& os, vtkIndent indent);

  vtkInformationInformationKey(const char* name, const char* location);
  ~vtkInformationInformationKey();

  // Description:
  // Get/Set the value associated with this key in the given
  // information object.
  void Set(vtkInformation* info, vtkInformation*);
  vtkInformation* Get(vtkInformation* info);
  int Has(vtkInformation* info);

private:
  vtkInformationInformationKey(const vtkInformationInformationKey&);  // Not implemented.
  void operator=(const vtkInformationInformationKey&);  // Not implemented.
};

#endif
