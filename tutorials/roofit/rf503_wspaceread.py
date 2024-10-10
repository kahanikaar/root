## \file
## \ingroup tutorial_roofit
## \notebook
## 'ORGANIZATION AND SIMULTANEOUS FITS' RooFit tutorial macro #503
##
## Reading and using a workspace
##
## The input file for self macro is generated by rf502_wspaceread.py
##
## \macro_image
## \macro_code
## \macro_output
##
## \date February 2018
## \authors Clemens Lange, Wouter Verkerke (C version)

import ROOT


# Read workspace from file
# -----------------------------------------------

# Open input file with workspace (generated by rf503_wspacewrite)
f = ROOT.TFile("rf502_workspace_py.root")

# Retrieve workspace from file
w = f.Get("w")

# Retrieve pdf, data from workspace
# -----------------------------------------------------------------

# Retrieve x, and data from workspace
x = w["x"]
model = w["model"]
data = w["modelData"]

# Print structure of composite p.d.f.
model.Print("t")

# Fit model to data, plot model
# ---------------------------------------------------------

# Fit model to data
model.fitTo(data, PrintLevel=-1)

# Plot data and PDF overlaid
xframe = x.frame(Title="Model and data read from workspace")
data.plotOn(xframe)
model.plotOn(xframe)

# Overlay the background component of model with a dashed line
model.plotOn(xframe, Components="bkg", LineStyle="--")

# Overlay the background+sig2 components of model with a dotted line
model.plotOn(xframe, Components="bkg,sig2", LineStyle=":")

# Draw the frame on the canvas
c = ROOT.TCanvas("rf503_wspaceread", "rf503_wspaceread", 600, 600)
ROOT.gPad.SetLeftMargin(0.15)
xframe.GetYaxis().SetTitleOffset(1.4)
xframe.Draw()

c.SaveAs("rf503_wspaceread.png")
