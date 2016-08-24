/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkValuePass.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkValuePass.h"

#include "vtkClearRGBPass.h"
#include "vtkInformation.h"
#include "vtkInformationDoubleVectorKey.h"
#include "vtkInformationIntegerKey.h"
#include "vtkInformationStringKey.h"
#include "vtkMapper.h"
#include "vtkObjectFactory.h"
#include "vtkProp.h"
#include "vtkRenderer.h"
#include "vtkRenderState.h"
#include "vtkSmartPointer.h"
#include "vtkRenderbuffer.h"
#include "vtkRenderWindow.h"
#include "vtkFloatArray.h"
#include "vtkOpenGLError.h"
#include "vtkOpenGLRenderWindow.h"
#include "vtkFrameBufferObject2.h"

#include <cassert>


vtkStandardNewMacro(vtkValuePass);

vtkInformationKeyMacro(vtkValuePass, RENDER_VALUES, Integer);
vtkInformationKeyMacro(vtkValuePass, SCALAR_MODE, Integer);
vtkInformationKeyMacro(vtkValuePass, ARRAY_MODE, Integer);
vtkInformationKeyMacro(vtkValuePass, ARRAY_ID, Integer);
vtkInformationKeyMacro(vtkValuePass, ARRAY_NAME, String);
vtkInformationKeyMacro(vtkValuePass, ARRAY_COMPONENT, Integer);
vtkInformationKeyMacro(vtkValuePass, SCALAR_RANGE, DoubleVector);

class vtkValuePass::vtkInternals
{
public:
  int FieldAssociation;
  int FieldAttributeType;
  std::string FieldName;
  bool FieldNameSet;
  int Component;
  double ScalarRange[2];
  bool ScalarRangeSet;

  // Array holder for FLOATING_POINT mode. The result pixels are downloaded
  // into this array.
  vtkSmartPointer<vtkFloatArray> Values;

  vtkInternals()
  : Values(NULL)
    {
    this->Values = vtkSmartPointer<vtkFloatArray>::New();
    this->Values->SetNumberOfComponents(1); /* GL_RED */

    this->FieldAssociation = 0;
    this->FieldAttributeType = 0;
    this->FieldName = "";
    this->FieldNameSet = false;
    this->Component = 0;
    this->ScalarRange[0] = 0.0;
    this->ScalarRange[1] = -1.0;
    this->ScalarRangeSet = false;
    }
};

// ----------------------------------------------------------------------------
vtkValuePass::vtkValuePass()
: ValueFrameBO(NULL)
, ValueRenderBO(NULL)
, DepthRenderBO(NULL)
, ValuePassResourcesAllocated(false)
, RenderingMode(INVERTIBLE_LUT)
{
  this->Internals = new vtkInternals();
}

// ----------------------------------------------------------------------------
vtkValuePass::~vtkValuePass()
{
  delete this->Internals;
}

// ----------------------------------------------------------------------------
void vtkValuePass::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);
}

// ----------------------------------------------------------------------------
void vtkValuePass::SetInputArrayToProcess(int fieldAssociation,
  const char *name)
{
  if (!this->Internals->FieldNameSet ||
      this->Internals->FieldAssociation != fieldAssociation ||
      this->Internals->FieldName.compare(name) != 0)
    {
    this->Internals->FieldAssociation = fieldAssociation;
    this->Internals->FieldName = name;
    this->Internals->FieldNameSet = true;
    this->Modified();
    }
}

// ----------------------------------------------------------------------------
void vtkValuePass::SetInputArrayToProcess(int fieldAssociation,
  int fieldAttributeType)
{
  if (this->Internals->FieldAssociation != fieldAssociation ||
      this->Internals->FieldAttributeType != fieldAttributeType ||
      this->Internals->FieldNameSet)
    {
    this->Internals->FieldAssociation = fieldAssociation;
    this->Internals->FieldAttributeType = fieldAttributeType;
    this->Internals->FieldNameSet = false;
    this->Modified();
    }
}

// ----------------------------------------------------------------------------
void vtkValuePass::SetInputComponentToProcess(int component)
{
  if (this->Internals->Component != component)
    {
    this->Internals->Component = component;
    this->Modified();
    }
}

// ----------------------------------------------------------------------------
void vtkValuePass::SetScalarRange(double min, double max)
{
  if (this->Internals->ScalarRange[0] != min ||
      this->Internals->ScalarRange[1] != max)
    {
    this->Internals->ScalarRange[0] = min;
    this->Internals->ScalarRange[1] = max;
    this->Internals->ScalarRangeSet = (max > min);
    this->Modified();
    }
}


// ----------------------------------------------------------------------------
// Description:
// Perform rendering according to a render state \p s.
// \pre s_exists: s!=0
void vtkValuePass::Render(const vtkRenderState *s)
{
  assert("pre: s_exists" && s!=0);

  this->BeginPass(s->GetRenderer());

  this->NumberOfRenderedProps = 0;
  this->RenderOpaqueGeometry(s);

  this->EndPass();
}

// ----------------------------------------------------------------------------
// Description:
// Opaque pass with key checking.
// \pre s_exists: s!=0
void vtkValuePass::RenderOpaqueGeometry(const vtkRenderState *s)
{
  assert("pre: s_exists" && s!=0);

  std::vector<int> scalarVisibilities;

  int c = s->GetPropArrayCount();
  int i = 0;
  while (i < c)
    {
    vtkProp *p = s->GetPropArray()[i];

    // Cache scalar visibility state and turn it on
    vtkActor *a = vtkActor::SafeDownCast(p);
    if (a)
      {
      scalarVisibilities.push_back(a->GetMapper()->GetScalarVisibility());
      a->GetMapper()->ScalarVisibilityOn();
      }

    vtkSmartPointer<vtkInformation> keys = p->GetPropertyKeys();
    if (!keys)
      {
      keys.TakeReference(vtkInformation::New());
      }
    keys->Set(vtkValuePass::RENDER_VALUES(), this->RenderingMode);
    keys->Set(vtkValuePass::SCALAR_MODE(), this->Internals->FieldAssociation);
    keys->Set(vtkValuePass::ARRAY_MODE(), this->Internals->FieldNameSet);
    keys->Set(vtkValuePass::ARRAY_ID(), this->Internals->FieldAttributeType);
    keys->Set(vtkValuePass::ARRAY_NAME(), this->Internals->FieldName.c_str());
    keys->Set(vtkValuePass::ARRAY_COMPONENT(), this->Internals->Component);
    keys->Set(vtkValuePass::SCALAR_RANGE(), this->Internals->ScalarRange, 2);
    p->SetPropertyKeys(keys);

    int rendered =
      p->RenderOpaqueGeometry(s->GetRenderer());
    this->NumberOfRenderedProps += rendered;
    ++i;
    }

  // Remove set keys
  i = 0;
  while (i < c)
    {
    vtkProp *p = s->GetPropArray()[i];

    // Restore scalar visibility state
    vtkActor *a = vtkActor::SafeDownCast(p);
    if (a)
      {
      a->GetMapper()->SetScalarVisibility(scalarVisibilities[i]);
      }

    vtkInformation *keys = p->GetPropertyKeys();
    keys->Remove(vtkValuePass::RENDER_VALUES());
    keys->Remove(vtkValuePass::SCALAR_MODE());
    keys->Remove(vtkValuePass::ARRAY_MODE());
    keys->Remove(vtkValuePass::ARRAY_ID());
    keys->Remove(vtkValuePass::ARRAY_NAME());
    keys->Remove(vtkValuePass::ARRAY_COMPONENT());
    keys->Remove(vtkValuePass::SCALAR_RANGE());
    p->SetPropertyKeys(keys);
    ++i;
    }
}

//------------------------------------------------------------------------------
void vtkValuePass::BeginPass(vtkRenderer* ren)
{
  switch(this->RenderingMode)
  {
  case vtkValuePass::FLOATING_POINT:
    // Allocate if necessary and bind frame buffer.
    if (this->HasWindowSizeChanged(ren))
      {
      this->ReleaseFloatingPointMode(ren);
      }

    if (this->InitializeFloatingPointMode(ren))
      {
      this->ValueFrameBO->SaveCurrentBindings();
      this->ValueFrameBO->Bind(GL_DRAW_FRAMEBUFFER);
      }
    break;

  case vtkValuePass::INVERTIBLE_LUT:
  default:
    // Cleanup in case FLOATING_POINT was active.
    this->ReleaseFloatingPointMode(ren);
    break;
  }

  // Clear buffers
  glClearDepth(1.0);
  glClearColor(0.0, 0.0, 0.0, 0.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

//------------------------------------------------------------------------------
void vtkValuePass::EndPass()
{
  switch(this->RenderingMode)
  {
  case vtkValuePass::FLOATING_POINT:
    // Unbind the float FBO and glReadPixels to host side.
    this->ValueFrameBO->UnBind(GL_DRAW_FRAMEBUFFER);
    break;

  case vtkValuePass::INVERTIBLE_LUT:
  default:
    // Nothing to do in this mode.
    break;
  }
}

//------------------------------------------------------------------------------
bool vtkValuePass::HasWindowSizeChanged(vtkRenderer* ren)
{
  if (!this->ValueFrameBO)
    {
    return true;
    }

  int* size = ren->GetSize();
  int* fboSize = this->ValueFrameBO->GetLastSize(false);

  return (fboSize[0] != size[0] || fboSize[1] != size[1]);
}

//------------------------------------------------------------------------------
bool vtkValuePass::InitializeFloatingPointMode(vtkRenderer* ren)
{
  if (this->ValuePassResourcesAllocated)
    {
    return true;
    }

  vtkRenderWindow* renWin = ren->GetRenderWindow();
  if (!this->IsFloatFBOSupported(renWin))
    {
    vtkWarningMacro("Switching to INVERTIBLE_LUT mode.");
    this->RenderingMode = vtkValuePass::INVERTIBLE_LUT;
    return false;
    }

  int* size = ren->GetSize();
  // Allocate FBO's Color attachment target
  this->ValueRenderBO = vtkRenderbuffer::New();
  this->ValueRenderBO->SetContext(renWin);
  // CreateColorAttachment formats the attachment RGBA32F by
  // default, this is what vtkValuePass expects.
  this->ValueRenderBO->CreateColorAttachment(size[0], size[1]);

  // Allocate FBO's depth attachment target
  this->DepthRenderBO = vtkRenderbuffer::New();
  this->DepthRenderBO->SetContext(renWin);
  this->DepthRenderBO->CreateDepthAttachment(size[0], size[1]);

  // Initialize the FBO into which the float value pass is rendered.
  this->ValueFrameBO = vtkFrameBufferObject2::New();
  this->ValueFrameBO->SetContext(renWin);
  this->ValueFrameBO->SaveCurrentBindings();
  this->ValueFrameBO->Bind(GL_FRAMEBUFFER);
  this->ValueFrameBO->InitializeViewport(size[0], size[1]);
  this->ValueFrameBO->GetLastSize(true); /*force a cached size update*/
  /* GL_COLOR_ATTACHMENT0 */
  this->ValueFrameBO->AddColorAttachment(GL_FRAMEBUFFER, 0, this->ValueRenderBO);
  this->ValueFrameBO->AddDepthAttachment(GL_FRAMEBUFFER, this->DepthRenderBO);

  // Verify FBO
  if(!this->ValueFrameBO->CheckFrameBufferStatus(GL_FRAMEBUFFER))
    {
    vtkErrorMacro("Failed to attach FBO.");
    this->ReleaseFloatingPointMode(ren);
    return false;
    }

  this->ValueFrameBO->UnBind(GL_FRAMEBUFFER);
  this->ValuePassResourcesAllocated = true;

  return true;
}

//-----------------------------------------------------------------------------
void vtkValuePass::ReleaseFloatingPointMode(vtkRenderer* ren)
{
  if (!this->ValuePassResourcesAllocated)
    {
    return;
    }

  vtkRenderWindow* renWin = ren->GetRenderWindow();
  renWin->MakeCurrent();

  // Cleanup FBO (grahpics resources cleaned internally)
  this->ValueFrameBO->Delete();
  this->ValueFrameBO = NULL;

  this->ValueRenderBO->Delete();
  this->ValueRenderBO = NULL;

  this->DepthRenderBO->Delete();
  this->DepthRenderBO = NULL;

  this->ValuePassResourcesAllocated = false;
}

//-----------------------------------------------------------------------------
bool vtkValuePass::IsFloatFBOSupported(vtkRenderWindow *renWin)
{
  vtkOpenGLRenderWindow *context = vtkOpenGLRenderWindow::SafeDownCast(renWin);
  if (!context)
    {
    vtkErrorMacro(<< "Support for " << renWin->GetClassName()
      << " not implemented");
    return false;
    }

#if GL_ES_VERSION_2_0 != 1
  bool contextSupport = vtkOpenGLRenderWindow::GetContextSupportsOpenGL32();
  if (!contextSupport)
    {
    vtkWarningMacro(<< "Context does not support OpenGL core profile 3.2. "
      << " Will check extension support.");
    }

  bool extSupport = glewIsSupported("GL_EXT_framebuffer_object") &&
    glewIsSupported("GL_ARB_texture_float");
  if (!extSupport)
    {
    vtkWarningMacro(<< "EXT_framebuffer_object or ARB_texture_float not"
      << " supported.");
    }

  return contextSupport || extSupport;
#else
  return true;
#endif
}

//------------------------------------------------------------------------------
vtkFloatArray* vtkValuePass::GetFloatImageDataArray(vtkRenderer* ren)
{
  vtkRenderWindow* renWin = ren->GetRenderWindow();
  renWin->MakeCurrent();

  //Allocate output array.
  int* size = this->ValueFrameBO->GetLastSize(false);
  this->Internals->Values->SetNumberOfTuples(size[0] * size[1]);

  // RGB channels are equivalent in the FBO (they all contain the rendered
  // values).
  this->GetFloatImageData(GL_RED, size[0], size[1],
    this->Internals->Values->GetVoidPointer(0));

  return this->Internals->Values;
}

//-------------------------------------------------------------------------------
void vtkValuePass::GetFloatImageData(int const format, int const width,
  int const height, void* data)
{
  // Prepare and bind value texture and FBO.
  this->ValueFrameBO->SaveCurrentBindings();
  this->ValueFrameBO->Bind(GL_READ_FRAMEBUFFER);

  GLint originalReadBuff;
  glGetIntegerv(GL_READ_BUFFER, &originalReadBuff);
  glReadBuffer(GL_COLOR_ATTACHMENT0);

  // Calling pack alignment ensures any window size can be grabbed.
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glClampColor(GL_CLAMP_READ_COLOR, GL_FALSE);

  glReadPixels(0, 0, width, height, format, GL_FLOAT,
    data);

  glReadBuffer(originalReadBuff);
  this->ValueFrameBO->UnBind(GL_READ_FRAMEBUFFER);

  vtkOpenGLCheckErrorMacro("Failed to read pixels from OpenGL buffer!");
}

//-------------------------------------------------------------------------------
std::vector<int> vtkValuePass::GetFloatImageExtents()
{
  int* size = this->ValueFrameBO->GetLastSize(false);

  std::vector<int> ext;
  ext.reserve(6);
  ext.push_back(0); ext.push_back(size[0] - 1);
  ext.push_back(0); ext.push_back(size[1] - 1);
  ext.push_back(0); ext.push_back(0);

  return ext;
}
