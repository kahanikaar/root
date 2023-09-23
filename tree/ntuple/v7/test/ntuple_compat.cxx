#include "ntuple_test.hxx"

TEST(RNTupleCompat, FeatureFlag)
{
   FileRaii fileGuard("test_ntuple_compat_feature_flag.root");

   RNTupleDescriptorBuilder descBuilder;
   descBuilder.SetNTuple("ntpl", "");
   descBuilder.SetFeature(RNTupleDescriptor::kFeatureFlagTest);
   descBuilder.AddField(
      RFieldDescriptorBuilder::FromField(ROOT::Experimental::RFieldZero()).FieldId(0).MakeDescriptor().Unwrap());
   ASSERT_TRUE(static_cast<bool>(descBuilder.EnsureValidDescriptor()));

   auto writer = RNTupleFileWriter::Recreate("ntpl", fileGuard.GetPath(), 0, ENTupleContainerFormat::kTFile);
   RNTupleSerializer serializer;

   auto ctx = serializer.SerializeHeaderV1(nullptr, descBuilder.GetDescriptor());
   auto buffer = std::make_unique<unsigned char[]>(ctx.GetHeaderSize());
   ctx = serializer.SerializeHeaderV1(buffer.get(), descBuilder.GetDescriptor());
   writer->WriteNTupleHeader(buffer.get(), ctx.GetHeaderSize(), ctx.GetHeaderSize());

   auto szFooter = serializer.SerializeFooterV1(nullptr, descBuilder.GetDescriptor(), ctx);
   buffer = std::make_unique<unsigned char[]>(szFooter);
   serializer.SerializeFooterV1(buffer.get(), descBuilder.GetDescriptor(), ctx);
   writer->WriteNTupleFooter(buffer.get(), szFooter, szFooter);

   writer->Commit();
   writer = nullptr;

   auto pageSource = RPageSource::Create("ntpl", fileGuard.GetPath());
   try {
      pageSource->Attach();
      FAIL() << "opening an RNTuple that uses an unsupported feature should fail";
   } catch (const RException &err) {
      EXPECT_THAT(err.what(), testing::HasSubstr("unsupported format feature: 137"));
   }
}
