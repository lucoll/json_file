// #include "TJSONFile.h"

void create()
{
  TJSONFile *f = new TJSONFile("f.json","recreate");
  auto hist = new TH1F("hist", "the histo", 10, 0., 1.);
  hist->Write();
  delete hist;

  auto hist2 = new TH1F("hist2", "the histo, take two", 10, 0., 1.);
  hist2->Write();
  delete hist2;

  auto hist3 = new TH1D("hist3", "the histo, take three", 10, 0., 1.);
  hist3->Write();
  delete hist3;

  printf("File name %s\n", f->GetName());

  //gDebug = 1;

  // file shoul be written, do not call destructor
  f->Close();

}
