#pragma GCC diagnostic ignored "-Wunused-parameter"

// Author: Sergey Linev 8.07.2022

/*************************************************************************
 * Copyright (C) 1995-2022, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//________________________________________________________________________
//
// The main motivation for the XML  format is to facilitate the
// communication with other non ROOT applications. Currently
// writing and reading XML files is limited to ROOT applications.
// It is our intention to develop a simple reader independent
// of the ROOT libraries that could be used as an example for
// real applications. One of possible approach with code generation
// is implemented in TXMLPlayer class.
//
// The XML format should be used only for small data volumes,
// typically histogram files, pictures, geometries, calibrations.
// The XML file is built in memory before being dumped to disk.
//
// Like for normal ROOT files, XML files use the same I/O mechanism
// exploiting the ROOT/CINT dictionary. Any class having a dictionary
// can be saved in XML format.
//
// This first implementation does not support subdirectories
// or Trees.
//
// The shared library libRXML.so may be loaded dynamically
// via gSystem->Load("libRXML"). This library is automatically
// loaded by the plugin manager as soon as a XML file is created
// via, eg
//   TFile::Open("file.xml","recreate");
// TFile::Open returns a TJSONFile object. When a XML file is open in write mode,
// one can use the normal TObject::Write to write an object in the file.
// Alternatively one can use the new functions TDirectoryFile::WriteObject and
// TDirectoryFile::WriteObjectAny to write a TObject* or any class not deriving
// from TObject.
//
// example of a session saving a histogram to a XML file
// =====================================================
//   TFile *f = TFile::Open("Example.xml","recreate");
//   TH1F *h = new TH1F("h","test",1000,-2,2);
//   h->FillRandom("gaus");
//   h->Write();
//   delete f;
//
// example of a session reading the histogram from the file
// ========================================================
//   TFile *f = TFile::Open("Example.xml");
//   TH1F *h = (TH1F*)f->Get("h");
//   h->Draw();
//
// A new option in the canvas "File" menu is available to save
// a TCanvas as a XML file. One can also do
//   canvas->Print("Example.xml");
//
// Configuring ROOT with the option "xml"
// ======================================
// The XML package is enabled by default
//
// documentation
// =============
// See also classes TBufferJSON, TKeyJSON
//
//______________________________________________________________________________

#include "TJSONFile.h"

#include "TROOT.h"
#include "TSystem.h"
#include "TList.h"
#include "TKeyJSON.h"
#include "TObjArray.h"
#include "TArrayC.h"
#include "TStreamerInfo.h"
#include "TStreamerElement.h"
#include "TProcessID.h"
#include "TError.h"
#include "TClass.h"
#include "TVirtualMutex.h"

#include <memory>
#include <fstream>
#include <nlohmann/json.hpp>
#include <iostream>

#include <iomanip>

ClassImp(TJSONFile);

////////////////////////////////////////////////////////////////////////////////
/// Open or creates local XML file with name filename.
/// It is recommended to specify filename as "<file>.xml". The suffix ".xml"
/// will be used by object browsers to automatically identify the file as
/// a XML file. If the constructor fails in any way IsZombie() will
/// return true. Use IsOpen() to check if the file is (still) open.
///
/// If option = NEW or CREATE   create a new file and open it for writing,
///                             if the file already exists the file is
///                             not opened.
///           = RECREATE        create a new file, if the file already
///                             exists it will be overwritten.
///           = 2xoo            create a new file with specified xml settings
///                             for more details see TXMLSetup class
///           = UPDATE          open an existing file for writing.
///                             if no file exists, it is created.
///           = READ            open an existing file for reading.
///
/// For more details see comments for TFile::TFile() constructor
///
/// TJSONFile does not support TTree objects

static constexpr int kCurrentFileFormatVersion = 1;

TJSONFile::TJSONFile(const char *filename, Option_t *option, const char *title, Int_t compression)
{
   if (!gROOT)
      ::Fatal("TFile::TFile", "ROOT system not initialized");

   if (filename && !strncmp(filename, "json:", 5)) // changed to json (+1)
      filename += 4;

   gDirectory = nullptr;
   SetName(filename);
   SetTitle(title);
   TDirectoryFile::Build(this, 0);

   fD = -1;
   fFile = this;
   fFree = nullptr;
   fVersion = gROOT->GetVersionInt(); // ROOT version in integer format
   fUnits = 4;
   fOption = option;
   SetCompressionSettings(compression);
   fWritten = 0;
   fSumBuffer = 0;
   fSum2Buffer = 0;
   fBytesRead = 0;
   fBytesWrite = 0;
   fClassIndex = 0;
   fSeekInfo = 0;
   fNbytesInfo = 0;
   fProcessIDs = nullptr;
   fNProcessIDs = 0;
   //fIOVersion = TJSONFile::Class_Version();
   fIOVersion = kCurrentFileFormatVersion;
   SetBit(kBinaryFile, kFALSE);

   fOption = option;
   fOption.ToUpper();

   if (fOption == "NEW")
      fOption = "CREATE";

   Bool_t create = (fOption == "CREATE") ? kTRUE : kFALSE;
   Bool_t recreate = (fOption == "RECREATE") ? kTRUE : kFALSE;
   Bool_t update = (fOption == "UPDATE") ? kTRUE : kFALSE;
   Bool_t read = (fOption == "READ") ? kTRUE : kFALSE;

   if (!create && !recreate && !update && !read)
   {
      read = kTRUE;
      fOption = "READ";
   }

   Bool_t devnull = kFALSE;
   const char *fname = nullptr;

   if (!filename || !filename[0])
   {
      Error("TJSONFile", "file name is not specified");
      goto zombie;
   }

   // support dumping to /dev/null on UNIX
   if (!strcmp(filename, "/dev/null") && !gSystem->AccessPathName(filename, kWritePermission))
   {
      devnull = kTRUE;
      create = kTRUE;
      recreate = kFALSE;
      update = kFALSE;
      read = kFALSE;
      fOption = "CREATE";
      SetBit(TFile::kDevNull);
   }

   gROOT->cd();

   fname = gSystem->ExpandPathName(filename);
   if (fname)
   {
      SetName(fname);
      delete[](char *) fname;
      fname = GetName();
   }
   else
   {
      Error("TJSONFile", "error expanding path %s", filename);
      goto zombie;
   }

   if (recreate)
   {
      if (!gSystem->AccessPathName(fname, kFileExists))
         gSystem->Unlink(fname);
      create = kTRUE;
      fOption = "CREATE";
   }

   if (create && !devnull && !gSystem->AccessPathName(fname, kFileExists))
   {
      Error("TJSONFile", "file %s already exists", fname);
      goto zombie;
   }

   if (update)
   {
      if (gSystem->AccessPathName(fname, kFileExists))
      {
         update = kFALSE;
         create = kTRUE;
      }
      if (update && gSystem->AccessPathName(fname, kWritePermission))
      {
         Error("TJSONFile", "no write permission, could not open file %s", fname);
         goto zombie;
      }
   }

   if (read)
   {
      if (gSystem->AccessPathName(fname, kFileExists))
      {
         Error("TJSONFile", "file %s does not exist", fname);
         goto zombie;
      }
      if (gSystem->AccessPathName(fname, kReadPermission))
      {
         Error("TJSONFile", "no read permission, could not open file %s", fname);
         goto zombie;
      }
   }

   fRealName = fname;

   if (create || update)
      SetWritable(kTRUE);
   else
      SetWritable(kFALSE);

   InitJsonFile(create);

   return;

zombie:
   MakeZombie();
   gDirectory = gROOT;
}

////////////////////////////////////////////////////////////////////////////////
/// initialize json file and correspondent structures
/// identical to TFile::Init() function

void TJSONFile::InitJsonFile(Bool_t create)
{
   Int_t len = gROOT->GetListOfStreamerInfo()->GetSize() + 1;
   if (len < 5000)
      len = 5000;
   fClassIndex = new TArrayC(len);
   fClassIndex->Reset(0);

   //fVersion = kCurrentFileFormatVersion;

   if (create)
   {

      fDoc = new nlohmann::json();
   }
   else
   {
      ReadFromFile();
   }

   {
      R__LOCKGUARD(gROOTMutex);
      gROOT->GetListOfFiles()->Add(this);
   }
   cd();

   fNProcessIDs = 0;
   TKey *key = nullptr;
   TIter iter(fKeys);
   while ((key = (TKey *)iter()) != nullptr)
   {
      if (!strcmp(key->GetClassName(), "TProcessID"))
         fNProcessIDs++;
   }

   fProcessIDs = new TObjArray(fNProcessIDs + 1);
}

////////////////////////////////////////////////////////////////////////////////
/// Close a XML file
/// For more comments see TFile::Close() function

void TJSONFile::Close(Option_t *option)
{
   printf("Close %d\n", IsOpen());

   if (!IsOpen())
      return;

   TString opt = option;
   if (opt.Length() > 0)
      opt.ToLower();

   if (IsWritable())
      SaveToFile();

   fWritable = kFALSE;

   if (fDoc)
   {
      delete (nlohmann::json *)fDoc;
      fDoc = nullptr;
   }

   if (fClassIndex)
   {
      delete fClassIndex;
      fClassIndex = nullptr;
   }

   {
      TDirectory::TContext ctxt(this);
      // Delete all supported directories structures from memory
      TDirectoryFile::Close();
   }

   // delete the TProcessIDs
   TList pidDeleted;
   TIter next(fProcessIDs);
   TProcessID *pid;
   while ((pid = (TProcessID *)next()))
   {
      if (!pid->DecrementCount())
      {
         if (pid != TProcessID::GetSessionProcessID())
            pidDeleted.Add(pid);
      }
      else if (opt.Contains("r"))
      {
         pid->Clear();
      }
   }
   pidDeleted.Delete();

   R__LOCKGUARD(gROOTMutex);
   gROOT->GetListOfFiles()->Remove(this);
}

////////////////////////////////////////////////////////////////////////////////
/// destructor of TJSONFile object

TJSONFile::~TJSONFile()
{
   Close();
}

////////////////////////////////////////////////////////////////////////////////
/// return kTRUE if file is opened and can be accessed

Bool_t TJSONFile::IsOpen() const
{
   return fDoc != nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// Reopen a file with a different access mode, like from READ to
/// See TFile::Open() for details

Int_t TJSONFile::ReOpen(Option_t *mode)
{
   cd();

   TString opt = mode;
   opt.ToUpper();

   if (opt != "READ" && opt != "UPDATE")
   {
      Error("ReOpen", "mode must be either READ or UPDATE, not %s", opt.Data());
      return 1;
   }

   if (opt == fOption || (opt == "UPDATE" && fOption == "CREATE"))
      return 1;

   if (opt == "READ")
   {
      // switch to READ mode

      if (IsOpen() && IsWritable())
         SaveToFile();
      fOption = opt;

      SetWritable(kFALSE);
   }
   else
   {
      fOption = opt;

      SetWritable(kTRUE);
   }

   return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// create json key, which will store object in json structures

TKey *TJSONFile::CreateKey(TDirectory *mother, const TObject *obj, const char *name, Int_t)
{
   return new TKeyJSON(mother, ++fKeyCounter, obj, name);
}

////////////////////////////////////////////////////////////////////////////////
/// create json key, which will store object in json structures

TKey *TJSONFile::CreateKey(TDirectory *mother, const void *obj, const TClass *cl, const char *name, Int_t)
{
   return new TKeyJSON(mother, ++fKeyCounter, obj, cl, name);
}

////////////////////////////////////////////////////////////////////////////////
/// function produces pair of xml and dtd file names

void TJSONFile::ProduceFileNames(const char *filename, TString &fname)
{
   fname = filename;

   Bool_t hasjsonext = kFALSE;

   if (fname.Length() > 5)
   {
      TString last = fname(fname.Length() - 5, 5);
      last.ToLower();
      hasjsonext = (last == ".json");
   }

   if (!hasjsonext)
      fname += ".json";
}

////////////////////////////////////////////////////////////////////////////////
/// Saves json structures to the file
/// jsomn elements are kept in list of TKeyJSON objects
/// When saving, all this elements are linked to root json node
/// At the end StreamerInfo structures are added
/// After json document is saved, all nodes will be unlinked from root node
/// and kept in memory.
/// Only Close() or destructor release memory, used by json structures

void TJSONFile::SaveToFile()
{
   if (gDebug > 0)
      Info("SaveToFile", "File: %s io %d", fRealName.Data(), GetIOVersion());
      //Info("SaveToFile", "File: %s io %d", fRealName.Data(),kCurrentFileFormatVersion);

   if (!fDoc)
      return;

   auto &rootNode = *((nlohmann::json *)fDoc);

   rootNode = nlohmann::json::object();

   if (TestBit(TFile::kReproducible))
      rootNode[jsonio::CreateTm] = TDatime((UInt_t)1).AsSQLString();
   else
      rootNode[jsonio::CreateTm] = fDatimeC.AsSQLString();

   if (TestBit(TFile::kReproducible))
      rootNode[jsonio::ModifyTm] = TDatime((UInt_t)1).AsSQLString();
   else
      rootNode[jsonio::ModifyTm] = fDatimeM.AsSQLString();

   if (TestBit(TFile::kReproducible))
      rootNode[jsonio::ObjectUUID] = TUUID("00000000-0000-0000-0000-000000000000").AsString();
   else
      rootNode[jsonio::ObjectUUID] = fUUID.AsString();

   rootNode[jsonio::Type] = "ROOTfile";
   rootNode["ROOTVersionCode"] = gROOT->GetVersionCode();
   rootNode[jsonio::IOVersion] =GetIOVersion();

   TString fname;
   ProduceFileNames(fRealName, fname);

   CombineNodesTree(this, &rootNode["Keys"], kTRUE);

   WriteStreamerInfo();

   // save document
   {
      std::ofstream o(fname.Data());
      o << std::setw(3) << rootNode << std::endl;
   }

}

////////////////////////////////////////////////////////////////////////////////
/// Connect/disconnect all file nodes to single tree before/after saving

void TJSONFile::CombineNodesTree(TDirectory *dir, void *topnode, Bool_t dolink)
{ 
    if (!dir)
       return;

       TIter iter(dir->GetListOfKeys());
       TKeyJSON *key = nullptr;
       //auto &topNodeJSON = *(nlohmann::json *)topnode;

       nlohmann::json infos_array = nlohmann::json::array();

       while ((key = (TKeyJSON *)iter()) != nullptr) {
         nlohmann::json topNodeJSON = nlohmann::json::object();
          if (dolink){
            
            topNodeJSON = *(nlohmann::json *)key->KeyNode(); 
          }
         // else
             //fXML->UnlinkNode(key->KeyNode());
          if (key->IsSubdir())
             CombineNodesTree(FindKeyDir(dir, key->GetKeyId()), key->KeyNode(), dolink);

         infos_array.push_back(topNodeJSON);
       }

        (*((nlohmann::json *)topnode))= infos_array;
    
}

////////////////////////////////////////////////////////////////////////////////
/// read document from file
/// Now full content of document reads into the memory
/// Then document decomposed to separate keys and streamer info structures
/// All irrelevant data will be cleaned

Bool_t TJSONFile::ReadFromFile()
{
   assert(!fDoc && "Expect fDoc == nullptr!");

   std::ifstream file(fRealName.Data());
   if (file.good())
   {
      try
      {
         fDoc = (void *)new nlohmann::json(nlohmann::json::parse(file));
      }
      catch (nlohmann::detail::parse_error const &e)
      {
         throw std::runtime_error(e.what());
      }
      auto &rootNode = *((nlohmann::json *)fDoc);
      if (!rootNode.contains(jsonio::Type))
         throw std::runtime_error("File does not have a type.");
      else if (rootNode[jsonio::Type] != "ROOTfile")
         throw std::runtime_error("Not a ROOT File.");
      else if (rootNode[jsonio::IOVersion] > kCurrentFileFormatVersion)
         throw std::runtime_error("File version not compatible.");
      else
      {
         TString fType = rootNode[jsonio::Type].get<std::string>();
         long int versionOfROOT = rootNode["ROOTVersionCode"].get<long int>();
         fIOVersion = rootNode[jsonio::IOVersion].get<int>();

         if (rootNode.contains(jsonio::CreateTm))
         {
            TDatime tm(rootNode[jsonio::CreateTm].get<std::string>().c_str());
            fDatimeC = tm;
         }
         if (rootNode.contains(jsonio::ModifyTm))
         {
            TDatime tm(rootNode[jsonio::ModifyTm].get<std::string>().c_str());
            fDatimeM = tm;
         }
         if (rootNode.contains(jsonio::ObjectUUID))
            fUUID = rootNode[jsonio::ObjectUUID].get<std::string>().c_str();

         if (rootNode.contains(jsonio::Title))
            SetTitle(rootNode[jsonio::Title].get<std::string>().c_str());

         //////////////////////////////////////////////////////////////

         if (rootNode.contains("StreamerInfos"))
            ReadStreamerInfo();

          ReadKeysList(this, &rootNode);
         std::cout << fType << "  " << fUUID.AsString() << std::endl;

         return kTRUE;
      }
   }
   else
      throw std::runtime_error("File does not exist.");

}

////////////////////////////////////////////////////////////////////////////////
/// Read list of keys for directory

Int_t TJSONFile::ReadKeysList(TDirectory *dir, void *topnode)
{
   if (!dir || !topnode)
      return 0;
         Int_t nkeys = 0;
         auto &rootnode = *((nlohmann::json *)topnode);
         auto &keynode = rootnode["Keys"];

         TList *list = new TList();

         while (keynode[nkeys].contains("Object")) {

               TKeyJSON *key = new TKeyJSON(dir, ++fKeyCounter, &keynode[nkeys]);
               dir->AppendKey(key);
               //if (gDebug > 0)
                 // Info("ReadKeysList", "Add key %s from node %s", key->GetName(), fXML->GetNodeName(keynode));

               nkeys++;
         }

         return nkeys;
      
}

////////////////////////////////////////////////////////////////////////////////
/// convert all TStreamerInfo, used in file, to xml format

void TJSONFile::WriteStreamerInfo()
{

   if (!IsStoreStreamerInfos())
      return;

   TObjArray list;

   TIter iter(gROOT->GetListOfStreamerInfo());

   TStreamerInfo *info = nullptr;

   while ((info = (TStreamerInfo *)iter()) != nullptr)
   {
      Int_t uid = info->GetNumber();
      if (fClassIndex->fArray[uid] || true) // just for debugging add all existing streamer infos
         list.Add(info);
   }

   if (list.GetSize() == 0)
      return;

   nlohmann::json infos_array = nlohmann::json::array();

   for (int n = 0; n <= list.GetLast(); n++)
   {
      info = (TStreamerInfo *)list.At(n);

      nlohmann::json infonode = nlohmann::json::object();

      infonode["name"] = info->GetName();
      infonode["title"] = info->GetTitle();
      infonode["classversion"] = info->GetClassVersion();
      infonode["checksum"] = info->GetCheckSum();
      infonode["canoptimize"]=(info->TestBit(TStreamerInfo::kCannotOptimize) ? jsonio::False : jsonio::True);

      TIter iter2(info->GetElements());
      infonode["elements"] = nlohmann::json::array();

      TStreamerElement *elem = nullptr;
      while ((elem = (TStreamerElement *)iter2()) != nullptr)
         StoreStreamerElement(&infonode, elem);

      infos_array.push_back(infonode);
   }

   (*((nlohmann::json *)fDoc))["StreamerInfos"] = infos_array;
}

////////////////////////////////////////////////////////////////////////////////
/// Read streamerinfo structures from json format and provide them in the list
/// It is user responsibility to destroy this list

TFile::InfoListRet TJSONFile::GetStreamerInfoListImpl(bool /* lookupSICache */)
{
   ROOT::Internal::RConcurrentHashColl::HashValue hash;

   auto &rootNode = *((nlohmann::json *)fDoc);
   auto &sinfonode= rootNode["StreamerInfos"];


   TList *list = new TList();
int i=0;
      while (sinfonode[i].contains("name")) {

            TString fname = sinfonode[i]["name"].get<std::string>();
            TString ftitle = sinfonode[i]["title"].get<std::string>();

            TStreamerInfo *info = new TStreamerInfo(TClass::GetClass(fname));
            info->SetTitle(ftitle);

            list->Add(info);

            Int_t clversion = sinfonode[i]["classversion"].get<int>();
            info->SetClassVersion(clversion);
            info->SetOnFileClassVersion(clversion);
            Int_t checksum = sinfonode[i]["checksum"].get<int>();
            info->SetCheckSum(checksum);

            const char *canoptimize = sinfonode[i]["canoptimize"].get<std::string>().c_str();

            if (!canoptimize || (strcmp(canoptimize, jsonio::False) == 0))
               info->SetBit(TStreamerInfo::kCannotOptimize);
            else
               info->ResetBit(TStreamerInfo::kCannotOptimize);

            //auto &node=sinfonode[i]["elements"];

            int j=0;
            while (sinfonode[i]["elements"][j].contains("name")) {
               ReadStreamerElement(&sinfonode[i]["elements"][j], info);
               j++;
            }
         
         i++;

      }

   list->SetOwner();

   return {list, 0, hash};
}

////////////////////////////////////////////////////////////////////////////////
/// store data of single TStreamerElement in streamer node

void TJSONFile::StoreStreamerElement(void *infonode, TStreamerElement *elem)
{

   TClass *cl = elem->IsA();
   nlohmann::json info = nlohmann::json::object();

   char sbuf[100], namebuf[100];

   info["streamerelement"] = cl->GetName(); 

   info["name"] = elem->GetName();

   if (strlen(elem->GetTitle()) > 0)
      info["title"] = elem->GetTitle();

   info["v"] = cl->GetClassVersion();

   info["type"] = elem->GetType();

   if (strlen(elem->GetTypeName()) > 0)
      info["typename"] = elem->GetTypeName();

   info["size"] = elem->GetSize();

   if (elem->GetArrayDim() > 0)
   {
      nlohmann::json arraydim = nlohmann::json::array();

      for (int ndim = 0; ndim < elem->GetArrayDim(); ndim++) {
         arraydim.push_back(elem->GetMaxIndex(ndim));
      }

      info["arraydim"] = arraydim;

   }
   if (cl == TStreamerBase::Class())
   {
      TStreamerBase *base = (TStreamerBase *)elem;
      info["baseversion"] = base->GetBaseVersion();
      info["basechecksum"] = base->GetBaseCheckSum();
   }
   else if (cl == TStreamerBasicPointer::Class())
   {
      TStreamerBasicPointer *bptr = (TStreamerBasicPointer *)elem;
      info["countversion"] = bptr->GetCountVersion();
      info["countname"] = bptr->GetCountName();
      info["countclass"] = bptr->GetCountClass();
   }
   else if (cl == TStreamerLoop::Class())
   {
      TStreamerLoop *loop = (TStreamerLoop *)elem;
      info["countversion"] = loop->GetCountVersion();
      info["countname"] = loop->GetCountName();
      info["countclass"] = loop->GetCountClass();
   }
   else if ((cl == TStreamerSTL::Class()) || (cl == TStreamerSTLstring::Class()))
   {
      TStreamerSTL *stl = (TStreamerSTL *)elem;
      info["STLtype"] = stl->GetSTLtype();
      info["Ctype"] = stl->GetCtype();
   }

   (*((nlohmann::json *)infonode))["elements"].push_back(info);

}

////////////////////////////////////////////////////////////////////////////////
/// read and reconstruct single TStreamerElement from json node

void TJSONFile::ReadStreamerElement(void *node, TStreamerInfo *info)
{
   auto &streamernode = *((nlohmann::json *)node);

      TClass *cl = TClass::GetClass(streamernode["streamerelement"].get<std::string>().c_str()); 
      if (!cl || !cl->InheritsFrom(TStreamerElement::Class()))
         return;
      TStreamerElement *elem = (TStreamerElement *)cl->New();

      int elem_type = streamernode["type"].get<int>();

      elem->SetName(streamernode["name"].get<std::string>().c_str());
      elem->SetTitle(streamernode["title"].get<std::string>().c_str());
      elem->SetType(elem_type);
      elem->SetTypeName(streamernode["typename"].get<std::string>().c_str());
      elem->SetSize(streamernode["size"].get<int>());

      if (cl == TStreamerBase::Class()) {
         int basever = streamernode["baseversion"].get<int>();
         ((TStreamerBase *)elem)->SetBaseVersion(basever);
         Int_t baseCheckSum = streamernode["basechecksum"].get<int>();
         ((TStreamerBase *)elem)->SetBaseCheckSum(baseCheckSum);
      } else if (cl == TStreamerBasicPointer::Class()) {
         TString countname = streamernode["countname"].get<std::string>();
         TString countclass = streamernode["countclass"].get<std::string>();
         Int_t countversion = streamernode["countversion"].get<int>();

         ((TStreamerBasicPointer *)elem)->SetCountVersion(countversion);
         ((TStreamerBasicPointer *)elem)->SetCountName(countname);
         ((TStreamerBasicPointer *)elem)->SetCountClass(countclass);
      } else if (cl == TStreamerLoop::Class()) {
         TString countname = streamernode["countname"].get<std::string>();
         TString countclass = streamernode["countclass"].get<std::string>();
         Int_t countversion = streamernode["countversion"].get<int>();

         ((TStreamerLoop *)elem)->SetCountVersion(countversion);
         ((TStreamerLoop *)elem)->SetCountName(countname);
         ((TStreamerLoop *)elem)->SetCountClass(countclass);
      } else if ((cl == TStreamerSTL::Class()) || (cl == TStreamerSTLstring::Class())) {
         int fSTLtype = streamernode["STLtype"].get<int>();
         int fCtype = streamernode["Ctype"].get<int>();
         ((TStreamerSTL *)elem)->SetSTLtype(fSTLtype);
         ((TStreamerSTL *)elem)->SetCtype(fCtype);
      }

      char namebuf[100];

      if (streamernode.contains("numdim")) {
         int numdim = streamernode["numdim"].get<int>();
         elem->SetArrayDim(numdim);
         for (int ndim = 0; ndim < numdim; ndim++) {
            sprintf(namebuf, "dim%d", ndim);
            int maxi = streamernode[namebuf].get<int>();
            elem->SetMaxIndex(ndim, maxi);
         }
      }

      elem->SetType(elem_type);
      elem->SetNewType(elem_type);

      info->GetElements()->Add(elem);
   
}

////////////////////////////////////////////////////////////////////////////////
/// If true, all correspondent to file TStreamerInfo objects will be stored in file
/// this allows to apply schema evolution later for this file
/// may be useful, when file used outside ROOT and TStreamerInfo objects does not required
/// Can be changed only for newly created file.

void TJSONFile::SetStoreStreamerInfos(Bool_t store)
{
   if (IsWritable() && (GetListOfKeys()->GetSize() == 0))
      fStoreStreamerInfos = store;
}

////////////////////////////////////////////////////////////////////////////////
/// Create key for directory entry in the key

Long64_t TJSONFile::DirCreateEntry(TDirectory *dir)
{
   TDirectory *mother = dir->GetMotherDir();
   if (!mother)
      mother = this;

   TKeyJSON *key = new TKeyJSON(mother, ++fKeyCounter, dir, dir->GetName(), dir->GetTitle());

   key->SetSubir();

   return key->GetKeyId();
}

////////////////////////////////////////////////////////////////////////////////
/// Search for key which correspond to directory dir

TKeyJSON *TJSONFile::FindDirKey(TDirectory *dir)
{
   TDirectory *motherdir = dir->GetMotherDir();
   if (!motherdir)
      motherdir = this;

   TIter next(motherdir->GetListOfKeys());
   TObject *obj = nullptr;

   while ((obj = next()) != nullptr)
   {
      TKeyJSON *key = dynamic_cast<TKeyJSON *>(obj);

      if (key)
         if (key->GetKeyId() == dir->GetSeekDir())
            return key;
   }

   return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// Find a directory in motherdir with a seek equal to keyid

TDirectory *TJSONFile::FindKeyDir(TDirectory *motherdir, Long64_t keyid)
{
   if (!motherdir)
      motherdir = this;

   TIter next(motherdir->GetList());
   TObject *obj = nullptr;

   while ((obj = next()) != nullptr)
   {
      TDirectory *dir = dynamic_cast<TDirectory *>(obj);
      if (dir)
         if (dir->GetSeekDir() == keyid)
            return dir;
   }

   return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
/// Read keys for directory
/// Make sense only once, while next time no new subnodes will be created

Int_t TJSONFile::DirReadKeys(TDirectory *dir)
{
   TKeyJSON *key = FindDirKey(dir);
   if (!key)
      return 0;

   return ReadKeysList(dir, key->KeyNode());
}

////////////////////////////////////////////////////////////////////////////////
/// Update key attributes

void TJSONFile::DirWriteKeys(TDirectory *)
{
   TIter next(GetListOfKeys());
   TObject *obj = nullptr;

   while ((obj = next()) != nullptr)
   {
      TKeyJSON *key = dynamic_cast<TKeyJSON *>(obj);
      if (key)
         key->UpdateAttributes();
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Write the directory header

void TJSONFile::DirWriteHeader(TDirectory *dir)
{
   TKeyJSON *key = FindDirKey(dir);
   if (key)
      key->UpdateObject(dir);
}
