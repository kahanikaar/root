import ROOT

#Set up model
#Declare observable x
x=ROOT.RooRealVar("x","x",0,11)

# Create two Gaussian PDFs g1(x,mean1,sigma) anf g2(x,mean2,sigma) and their parameters
mean = ROOT.RooRealVar("mean", "mean of gaussians", 5)
sigma1 = ROOT.RooRealVar("sigma1", "width of gaussians", 0.5)
sigma2 = ROOT.RooRealVar("sigma2", "width of gaussians", 1)


sig1=ROOT.RooGaussian("sig1","Signal component 1",x,mean,sigma1)
sig2=ROOT.RooGaussian("sig2","Signal component 2",x,mean,sigma2)

#Build Chebychev polynomial pdf
a0=ROOT.RooRealVar("a0","a0",0.5,1.,1.)
a1=ROOT.RooRealVar("a1","a1",0.2,1.,1.)
bkg=ROOT.RooChebychev("bkg",'Background',x,ROOT.RooArgSet(a0,a1))

#Sum the signal components into a composite signal pdf
sig1frac= ROOT.RooRealVar("sig1frac","fraction of component 1 in signal",0.8,0.,1.)
sig=ROOT.RooAddPdf("sig","Signal",ROOT.RooArgList(sig1,sig2),sig1frac)

#E x t e n d   t h e   p d f s
#  -----------------------------
# Define signal range in which events counts are to be defined

x.setRange("signalRange",4,6)

#Associated nsig/nbkg as expected number of events with sig/bkg _in_the_range_ "signalRange"
nsig=ROOT.RooRealVar("nsig","number of signal events in signalRange",500,0.,10000)
nbkg=ROOT.RooRealVar("nbkg","number of background events in signalRange",500,0,10000)

#Use AddPdf to extend the model. Giving as many coefficients as pdfs switches
#on extension.
model=ROOT.RooAddPdf("model","(g1+g2)+a", ROOT.RooArgList(bkg,sig), ROOT.RooArgList(nbkg,nsig))

#S a m p l e   d a t a ,   f i t   m o d e l
#-------------------------------------------
#Generate 1000 events from model so that nsig,nbkg come out to numbers <<500 in fit
data = ROOT.RooDataSet(model.generate(x,1000))
c = ROOT.TCanvas("Canvas", "Canvas", 1500, 600)
c.Divide(3,1)

#Fit full range
# -------------------------------------------
c.cd(1)

#Perform unbinned ML fit to data, full range

#   IMPORTANT:
#   The model needs to be copied when fitting with different ranges because
#   the interpretation of the coefficients is tied to the fit range
#   that's used in the first fit

model1=ROOT.RooAddPdf(model)
r=ROOT.RooFitResult(model1.fitTo(data,ROOT.RooFit.Save()))

frame=x.frame(ROOT.RooFit.Title("Full range fitted"))
data.plotOn(frame)
model1.plotOn(frame,ROOT.RooFit.VisualizeError(r))
model1.plotOn(frame)
model1.paramOn(frame)
frame.Draw()

# Fit in two regions
#-------------------------------------------

c.cd(2)
x.setRange("left",  0., 4.)
x.setRange("right", 6., 10.)

model2=ROOT.RooAddPdf(model)
r2=ROOT.RooFitResult(model2.fitTo(data,
      ROOT.RooFit.Range("left,right"),
      ROOT.RooFit.Save()))
r2.Print()

frame2=x.frame(ROOT.RooFit.Title("Fit in left/right sideband"))
data.plotOn(frame2)
model2.plotOn(frame2, ROOT.RooFit.VisualizeError(r2))
model2.plotOn(frame2)
model2.paramOn(frame2)
frame2.Draw()

 #Fit in one region
 # -------------------------------------------
 # Note how restricting the region to only the left tail increases
 # the fit uncertainty

c.cd(3)
x.setRange("leftToMiddle",  0., 5.)

model3=ROOT.RooAddPdf(model)
r3=ROOT.RooFitResult(model3.fitTo(data,
      ROOT.RooFit.Range("leftToMiddle"),
      ROOT.RooFit.Save()))
r3.Print()

frame3 = x.frame(ROOT.RooFit.Title("Fit from left to middle"))
data.plotOn(frame3)
model3.plotOn(frame3, ROOT.RooFit.VisualizeError(r3))
model3.plotOn(frame3)
model3.paramOn(frame3)
frame3.Draw()
c.Draw()
c.SaveAs("rf204a_extendedLikelihood.png")
