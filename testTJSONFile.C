#include "gtest/gtest.h"

#include "TFile.h"
#include <iostream>
#include <fstream>
#include "TROOT.h"
#include "TUUID.h"
#include </home/lcollsar/ROOT/buildROOT/include/nlohmann/json.hpp>

#include <tuple>
#include <string>
#include <vector>

#include <algorithm>
#include <gmock/gmock-matchers.h>

using json = nlohmann::json;
using JSONKey_t = std::tuple<std::string /*name*/, std::string /*title*/, long long /*date*/, long long /*time*/>;

class TJSONFile1 final : public TFile {

public:
   static constexpr int kCurrentFileFormatVersion = 1;
   TJSONFile1() { printf("In constructor - no arguments \n"); }
   TJSONFile1(const char *filename, Option_t *option)
      : fFilename(filename), fOption(option) //, fVersion(kCurrentFileFormatVersion)
   {
      printf("IN CONSTRUCTOR \n");
      if (fOption != "READ" && fOption != "CREATE" && fOption != "RECREATE" && fOption != "UPLOAD")
         throw std::runtime_error("Not an option.");

      if (fOption == "READ") {
         std::ifstream file(fFilename);
         json jf;
         if (file.good()) {
            try {
               jf = json::parse(file);
            } catch (nlohmann::detail::parse_error const &e) {
               throw std::runtime_error(e.what());
            }
            printf("parsed \n");

            if (!jf.contains("type"))
               throw std::runtime_error("File does not have a type.");
            else if (jf["type"] != "ROOT file")
               throw std::runtime_error("Not a ROOT File.");
            else if (jf["JSONFile version"] > kCurrentFileFormatVersion)
               throw std::runtime_error("File version not compatible.");
            else {
               TString fType = jf["type"].get<std::string>();
               fUUID = jf["UUID"].get<std::string>().c_str();
               // Once TFileJSON needs to evolve its schema:
               //  long int versionOfROOT = jf["ROOT version code"].get<long int>();
               fVersion = jf["JSONFile version"].get<int>();

               // std::vector<JSONKey_t> keys;
               // keys = jf["keys"].get<std::vector<JSONKey_t>>();
               std::cout << fType << "  " << fVersion << "  " << fUUID.AsString() << std::endl;
               // std::cout << "keys sizee: " << keys.size() << std::endl;

               // for (int i = 0; i < keys.size(); i++) {
               // JSONKey_t k1 = keys.at(i);
               //  std::cout << std::get<0>(k1) << " "<< std::get<1>(k1) << " "<< std::get<2>(k1) << " "<<
               //  std::get<3>(k1) << " "<< std::endl; this works but has no sense since we need the "":"". We cant
               //  access with json to the inside values
               //}
               int i=1;
               std::string keytitle = "key" + std::to_string(i);
               while(jf.contains(keytitle)) {
                  TString keyname = jf[keytitle]["fName"].get<std::string>();
                  std::cout << keytitle << " " << keyname << std::endl; // works
                  i++;
                  keytitle = "key" + std::to_string(i);
               }
            }
         } else
            throw std::runtime_error("File does not exist.");
         printf(fOption, "\n");
      }
   }
   TString fFilename = "hi again \n";
   TString fOption;

   ~TJSONFile1()
   {
      printf("IN DESTRUCTOR \n");
      fVersion = kCurrentFileFormatVersion;

      if (fOption == "CREATE") {
         // Check if file exist
         std::ifstream file(fFilename);
         if (file.good())
            throw std::runtime_error("File does exist.");
         else {
            // CHANGE THIS TO NLOHMANN
            std::ofstream forwrite;
            forwrite.open(fFilename);
            forwrite << "{\n"
                     << " \"type\": \"ROOT file\",\n"
                     << 
               " \"ROOT version code\": " << gROOT->GetVersionCode() << ",\n"
                     << " \"JSONFile version\": 1,\n";

            if (TestBit(kReproducible))
               forwrite << "\"UUID\": \"00000000-0000-0000-0000-000000000000\"\n";
            else
               forwrite << "\"UUID\": \"" << fUUID.AsString() << "\"\n";

            forwrite << "}\n";

            printf("%s\n", fUUID.AsString());
            forwrite.close();
         }
         printf(fOption, "\n");
      }
      if (fOption == "RECREATE") {
         std::ofstream forwrite;
         forwrite.open(fFilename);

         json j;
         j["type"] = "ROOT file";
         j["ROOT version code"] = gROOT->GetVersionCode();
         j["JSONFile version"] = fVersion;
         if (TestBit(kReproducible))
            j["UUID"] = TUUID("00000000-0000-0000-0000-000000000000").AsString();
         else
            j["UUID"] = fUUID.AsString();

         // std::vector<JSONKey_t> keys;
         // keys.push_back(JSONKey_t{"aName", "aTitle", 123, 456});
         // keys.push_back(JSONKey_t{"aName2", "aTitle2", 1232, 4562});

         // j["keys"] = keys;

         // json thekeys;
         // thekeys["name"] = "aName";
         // thekeys["title"] = "aTitle";
         // thekeys["num1"] = 123;
         // thekeys["num2"] = 456;

         // j["thekey"] = thekeys; // this could be loops changing the name of the key

         TFile *f = new TFile("/home/lcollsar/ROOT/demo.root");
         TIter next(f->GetListOfKeys());
         TObject *obj = nullptr;

         int i = 1;
         while ((obj = next()) != nullptr) {
            TKey *key = dynamic_cast<TKey *>(obj);

            if (!key)
               continue;

            json keyaux;
            keyaux["fName"] = key->GetName();
            keyaux["fTitle"] = key->GetTitle();
            keyaux["fClassName"] = key->GetClassName();
            keyaux["fCycle"] = key->GetCycle();
            keyaux["fKeylen"] = key->GetKeylen();
            //Add any key or all keys

            //TDatime datime = key-> GetDatime();
            //printf("fDatime: ", datime);

            std::string keytitle = "key" + std::to_string(i);
            j[keytitle] = keyaux; 
            i++;
         }

         forwrite << j;

         printf(fOption, "\n"); 
      }
      if (fOption == "UPDATE") {
         // Read file and writes below
         std::ifstream file(fFilename);
         if (!file.good())
            printf("File does not exists");
         else {
            json jf = json::parse(file);
         }
         printf(fOption, "\n");
      }
   }

   // protected:

   // ClassDefOverride(TJSONFile11, 3) // ROOT file in JSON format
};

//void testJSON(const char *filename, Option_t *option){
//   TJSONFile1 file(filename, option);
//}

TEST(TJSONFileTests, BadFiles)
{
   {
      // TJSONFile1 file("doesnotexist.json", "READ");
      EXPECT_THROW(
         {
            try {
               TJSONFile1 file("doesnotexist.json", "READ");
            } catch (std::runtime_error const &e) {
               EXPECT_EQ(std::string("File does not exist."), e.what());
               throw;
            }
         },
         std::runtime_error); // works
   }
    {
       // TJSONFile1 file("notajsonfile.txt", "READ");
       EXPECT_THROW(
          {
             try {
                TJSONFile1 file("notajsonfile.txt", "READ");
             } catch (std::runtime_error const &e) {
                EXPECT_THAT(e.what(), HasSubstr("parse error at line 1"));
                throw;
             }
          },
          std::runtime_error);
       // EXPECT_THROW(TJSONFile1 file("notajsonfile.txt", "READ"),std::runtime_error);
    }
   {
      // TJSONFile1 file("notarootfile.json", "READ");
      EXPECT_THROW(
         {
            try {
               TJSONFile1 file("notarootfile.json", "READ");
            } catch (std::runtime_error const &e) {
               EXPECT_EQ(std::string("Not a ROOT File."), e.what());
               throw;
            }
         },
         std::runtime_error); // works
   }
   {
      // TJSONFIle format version 999999
      EXPECT_THROW(
         {
            try {
               TJSONFile1 file("filefromthefuture.json", "READ");
            } catch (std::runtime_error const &e) {
               EXPECT_EQ(std::string("File version not compatible."), e.what());
               throw;
            }
         },
         std::runtime_error); // works
   }
}

TEST(TJSONFileTests, ConstructorArguments)
{
   {
      EXPECT_THROW(
         {
            try {
               TJSONFile1 file("notajsonfile.txt", "NOTANOPTION");
            } catch (std::runtime_error const &e) {
               EXPECT_EQ(std::string("Not an option."), e.what());
               throw;
            }
         },
         std::runtime_error); // works just the notanoption (think it should be first)
   }
}

TEST(TJSONFileTests, FileHeader)
{
   TUUID uuidWritten;
   {
      TJSONFile1 file("test.json", "RECREATE");
      uuidWritten = file.GetUUID();
      printf("I'm here \n");
   }

   TJSONFile1 file("test.json", "READ");
   printf("I'm here 2\n");

   EXPECT_EQ(file.GetVersion(), 1);        // works
   EXPECT_EQ(file.GetUUID(), uuidWritten); // works
}

TEST(TJSONFileTests, FileHeaderReproducible)
{
   {
      TJSONFile1 file("testrepro.json", "RECREATE");
      file.SetBit(TFile::kReproducible);
   }

   TJSONFile1 file("testrepro.json", "READ");
   EXPECT_EQ(file.GetVersion(), 1);                                          // works
   EXPECT_EQ(file.GetUUID(), TUUID("00000000-0000-0000-0000-000000000000")); // works
}