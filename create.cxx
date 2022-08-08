#include "TJSONFile.h"

void create()
{
  TJSONFile *f = new TJSONFile("f.json","recreate");

  printf("File name %s\n", f->GetName());

  // file shoul be written, do not call destructor
  f->Close();

}
