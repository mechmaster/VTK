catch {load vtktcl}
# get the interactor ui
source ../../examplesTcl/vtkInt.tcl
source ../../examplesTcl/colors.tcl

# Create the RenderWindow, Renderer and both Actors
#
vtkRenderer ren1
vtkRenderWindow renWin
    renWin AddRenderer ren1
vtkRenderWindowInteractor iren
    iren SetRenderWindow renWin

# create pipeline
#
vtkVolume16Reader v16
  v16 SetDataDimensions 128 128
  [v16 GetOutput] SetOrigin 0.0 0.0 0.0
  v16 SetDataByteOrderToLittleEndian
  v16 SetFilePrefix "../../../vtkdata/headsq/half"
  v16 SetImageRange 1 93
  v16 SetDataSpacing 1.6 1.6 1.5
  v16 Update

vtkImageMarchingCubes iso
  iso SetInput [v16 GetOutput]
  iso SetValue 0 1150
  iso SetInputMemoryLimit 1000

vtkPolyDataMapper isoMapper
  isoMapper SetInput [iso GetOutput]
  isoMapper ScalarVisibilityOff
  isoMapper ImmediateModeRenderingOn

vtkActor isoActor
  isoActor SetMapper isoMapper
  eval [isoActor GetProperty] SetColor $antique_white

vtkOutlineFilter outline
  outline SetInput [v16 GetOutput]
vtkPolyDataMapper outlineMapper
  outlineMapper SetInput [outline GetOutput]
vtkActor outlineActor
  outlineActor SetMapper outlineMapper
  outlineActor VisibilityOff

# Add the actors to the renderer, set the background and size
#
ren1 AddActor outlineActor
ren1 AddActor isoActor
ren1 SetBackground 0.2 0.3 0.4
renWin SetSize 450 450
[ren1 GetActiveCamera] Elevation 90
[ren1 GetActiveCamera] SetViewUp 0 0 -1
[ren1 GetActiveCamera] Azimuth 180
iren Initialize

# render the image
#
iren SetUserMethod {wm deiconify .vtkInteract}
#renWin SetFileName "mcubes.tcl.ppm"
#renWin SaveImageAsPPM

# prevent the tk window from showing up then start the event loop
wm withdraw .


