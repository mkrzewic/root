#include "ntuple_test.hxx"

TEST(RNTuple, Basics)
{
   FileRaii fileGuard("test_ntuple_barefile.ntuple");

   auto model = RNTupleModel::Create();
   auto wrPt = model->MakeField<float>("pt", 42.0);

   {
      RNTupleWriteOptions options;
      options.SetContainerFormat(ENTupleContainerFormat::kBare);
      auto ntuple = RNTupleWriter::Recreate(std::move(model), "f", fileGuard.GetPath(), options);
      ntuple->Fill();
      ntuple->CommitCluster();
      *wrPt = 24.0;
      ntuple->Fill();
      *wrPt = 12.0;
      ntuple->Fill();
   }

   auto ntuple = RNTupleReader::Open("f", fileGuard.GetPath());
   EXPECT_EQ(3U, ntuple->GetNEntries());
   auto rdPt = ntuple->GetModel()->GetDefaultEntry()->Get<float>("pt");

   ntuple->LoadEntry(0);
   EXPECT_EQ(42.0, *rdPt);
   ntuple->LoadEntry(1);
   EXPECT_EQ(24.0, *rdPt);
   ntuple->LoadEntry(2);
   EXPECT_EQ(12.0, *rdPt);
}

TEST(RNTuple, Extended)
{
   FileRaii fileGuard("test_ntuple_barefile_ext.ntuple");

   auto model = RNTupleModel::Create();
   auto wrVector = model->MakeField<std::vector<double>>("vector");

   TRandom3 rnd(42);
   double chksumWrite = 0.0;
   {
      RNTupleWriteOptions options;
      options.SetContainerFormat(ENTupleContainerFormat::kBare);
      auto ntuple = RNTupleWriter::Recreate(std::move(model), "f", fileGuard.GetPath(), options);
      constexpr unsigned int nEvents = 32000;
      for (unsigned int i = 0; i < nEvents; ++i) {
         auto nVec = 1 + floor(rnd.Rndm() * 1000.);
         wrVector->resize(nVec);
         for (unsigned int n = 0; n < nVec; ++n) {
            auto val = 1 + rnd.Rndm()*1000. - 500.;
            (*wrVector)[n] = val;
            chksumWrite += val;
         }
         ntuple->Fill();
         if (i % 1000 == 0)
            ntuple->CommitCluster();
      }
   }

   auto ntuple = RNTupleReader::Open("f", fileGuard.GetPath());
   auto rdVector = ntuple->GetModel()->GetDefaultEntry()->Get<std::vector<double>>("vector");

   double chksumRead = 0.0;
   for (auto entryId : *ntuple) {
      ntuple->LoadEntry(entryId);
      for (auto v : *rdVector)
         chksumRead += v;
   }
   EXPECT_EQ(chksumRead, chksumWrite);
}

TEST(RPageSinkBuf, Basics)
{
   FileRaii fileGuard("test_ntuple_sinkbuf_basics.root");
   {
      auto model = RNTupleModel::Create();
      auto fieldPt = model->MakeField<float>("pt", 42.0);
      // PageSinkBuf wraps a concrete page source
      std::unique_ptr<RPageSink> sink = std::make_unique<RPageSinkBuf>(
         std::make_unique<RPageSinkFile>("myNTuple", fileGuard.GetPath(), RNTupleWriteOptions())
      );
      //std::unique_ptr<RPageSink> sink = std::make_unique<RPageSinkFile>(
      //   "myNTuple", fileGuard.GetPath(), RNTupleWriteOptions()
      //);
      sink->Create(*model.get());
      sink->CommitDataset();
      model = nullptr;
   }

   auto ntuple = RNTupleReader::Open("myNTuple", fileGuard.GetPath());
   ntuple->PrintInfo();
   auto viewPt = ntuple->GetView<float>("pt");
   int n = 0;
   for (auto i : ntuple->GetEntryRange()) {
      EXPECT_EQ(42.0, viewPt(i));
      n++;
   }
   EXPECT_EQ(1, n);
}
