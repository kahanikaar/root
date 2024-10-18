/// \file RNTupleModel.cxx
/// \ingroup NTuple ROOT7
/// \author Jakob Blomer <jblomer@cern.ch>
/// \date 2018-10-15
/// \warning This is part of the ROOT 7 prototype! It will change without notice. It might trigger earthquakes. Feedback
/// is welcome!

/*************************************************************************
 * Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include <ROOT/RError.hxx>
#include <ROOT/RField.hxx>
#include <ROOT/RNTupleModel.hxx>
#include <ROOT/RNTupleWriter.hxx>
#include <ROOT/StringUtils.hxx>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <utility>

namespace {
std::uint64_t GetNewModelId()
{
   static std::atomic<std::uint64_t> gLastModelId = 0;
   return ++gLastModelId;
}
} // anonymous namespace

ROOT::Experimental::Internal::RProjectedFields &
ROOT::Experimental::Internal::GetProjectedFieldsOfModel(ROOT::Experimental::RNTupleModel &model)
{
   return *model.fProjectedFields;
}

//------------------------------------------------------------------------------

ROOT::Experimental::RResult<void>
ROOT::Experimental::Internal::RProjectedFields::EnsureValidMapping(const RFieldBase *target, const FieldMap_t &fieldMap)
{
   auto source = fieldMap.at(target);
   const bool hasCompatibleStructure =
      (source->GetStructure() == target->GetStructure()) ||
      ((source->GetStructure() == ENTupleStructure::kCollection) && dynamic_cast<const RCardinalityField *>(target));
   if (!hasCompatibleStructure)
      return R__FAIL("field mapping structural mismatch: " + source->GetFieldName() + " --> " + target->GetFieldName());
   if ((source->GetStructure() == ENTupleStructure::kLeaf) || (source->GetStructure() == ENTupleStructure::kUnsplit)) {
      if (target->GetTypeName() != source->GetTypeName())
         return R__FAIL("field mapping type mismatch: " + source->GetFieldName() + " --> " + target->GetFieldName());
   }

   auto fnHasArrayParent = [](const RFieldBase &f) -> bool {
      auto parent = f.GetParent();
      while (parent) {
         if (parent->GetNRepetitions() > 0)
            return true;
         parent = parent->GetParent();
      }
      return false;
   };
   if (fnHasArrayParent(*source) || fnHasArrayParent(*target)) {
      return R__FAIL("unsupported field mapping across fixed-size arrays");
   }

   // We support projections only across records and collections. In the following, we check that the projected
   // field is on the same path of collection fields in the field tree than the source field.

   // Finds the first non-record parent field of the input field
   auto fnBreakPoint = [](const RFieldBase *f) -> const RFieldBase * {
      auto parent = f->GetParent();
      while (parent) {
         if ((parent->GetStructure() != ENTupleStructure::kRecord) &&
             (parent->GetStructure() != ENTupleStructure::kLeaf)) {
            return parent;
         }
         parent = parent->GetParent();
      }
      // We reached the zero field
      return nullptr;
   };

   // If source or target has a variant or reference as a parent, error out
   auto *sourceBreakPoint = fnBreakPoint(source);
   if (sourceBreakPoint && sourceBreakPoint->GetStructure() != ENTupleStructure::kCollection)
      return R__FAIL("unsupported field mapping (source structure)");
   auto *targetBreakPoint = fnBreakPoint(target);
   if (targetBreakPoint && sourceBreakPoint->GetStructure() != ENTupleStructure::kCollection)
      return R__FAIL("unsupported field mapping (target structure)");

   if (!sourceBreakPoint && !targetBreakPoint) {
      // Source and target have no collections as parent
      return RResult<void>::Success();
   }
   if (sourceBreakPoint && targetBreakPoint) {
      if (sourceBreakPoint == targetBreakPoint) {
         // Source and target are children of the same collection
         return RResult<void>::Success();
      }
      if (auto it = fieldMap.find(targetBreakPoint); it != fieldMap.end() && it->second == sourceBreakPoint) {
         // The parent collection of parent is mapped to the parent collection of the source
         return RResult<void>::Success();
      }
      // Source and target are children of different collections
      return R__FAIL("field mapping structure mismatch: " + source->GetFieldName() + " --> " + target->GetFieldName());
   }

   // Either source or target have no collection as a parent, but the other one has; that doesn't fit
   return R__FAIL("field mapping structure mismatch: " + source->GetFieldName() + " --> " + target->GetFieldName());
}

ROOT::Experimental::RResult<void>
ROOT::Experimental::Internal::RProjectedFields::Add(std::unique_ptr<RFieldBase> field, const FieldMap_t &fieldMap)
{
   auto result = EnsureValidMapping(field.get(), fieldMap);
   if (!result)
      return R__FORWARD_ERROR(result);
   for (const auto &f : *field) {
      result = EnsureValidMapping(&f, fieldMap);
      if (!result)
         return R__FORWARD_ERROR(result);
   }

   fFieldMap.insert(fieldMap.begin(), fieldMap.end());
   fFieldZero->Attach(std::move(field));
   return RResult<void>::Success();
}

const ROOT::Experimental::RFieldBase *
ROOT::Experimental::Internal::RProjectedFields::GetSourceField(const RFieldBase *target) const
{
   if (auto it = fFieldMap.find(target); it != fFieldMap.end())
      return it->second;
   return nullptr;
}

std::unique_ptr<ROOT::Experimental::Internal::RProjectedFields>
ROOT::Experimental::Internal::RProjectedFields::Clone(const RNTupleModel *newModel) const
{
   auto cloneFieldZero = std::unique_ptr<RFieldZero>(static_cast<RFieldZero *>(fFieldZero->Clone("").release()));
   auto clone = std::unique_ptr<RProjectedFields>(new RProjectedFields(std::move(cloneFieldZero)));
   clone->fModel = newModel;
   // TODO(jblomer): improve quadratic search to re-wire the field mappings given the new model and the cloned
   // projected fields. Not too critical as we generally expect a limited number of projected fields
   for (const auto &[k, v] : fFieldMap) {
      for (const auto &f : *clone->GetFieldZero()) {
         if (f.GetQualifiedFieldName() == k->GetQualifiedFieldName()) {
            clone->fFieldMap[&f] = &clone->fModel->GetField(v->GetQualifiedFieldName());
            break;
         }
      }
   }
   return clone;
}

ROOT::Experimental::RNTupleModel::RUpdater::RUpdater(RNTupleWriter &writer)
   : fWriter(writer), fOpenChangeset(fWriter.GetUpdatableModel())
{
}

void ROOT::Experimental::RNTupleModel::RUpdater::BeginUpdate()
{
   fOpenChangeset.fModel.Unfreeze();
   // We set the model ID to zero until CommitUpdate(). That prevents calls to RNTupleWriter::Fill() in the middle
   // of updates
   std::swap(fOpenChangeset.fModel.fModelId, fNewModelId);
}

void ROOT::Experimental::RNTupleModel::RUpdater::CommitUpdate()
{
   fOpenChangeset.fModel.Freeze();
   std::swap(fOpenChangeset.fModel.fModelId, fNewModelId);
   if (fOpenChangeset.IsEmpty())
      return;
   Internal::RNTupleModelChangeset toCommit{fOpenChangeset.fModel};
   std::swap(fOpenChangeset.fAddedFields, toCommit.fAddedFields);
   std::swap(fOpenChangeset.fAddedProjectedFields, toCommit.fAddedProjectedFields);
   fWriter.GetSink().UpdateSchema(toCommit, fWriter.GetNEntries());
}

void ROOT::Experimental::RNTupleModel::RUpdater::AddField(std::unique_ptr<RFieldBase> field)
{
   auto fieldp = field.get();
   fOpenChangeset.fModel.AddField(std::move(field));
   fOpenChangeset.fAddedFields.emplace_back(fieldp);
}

ROOT::Experimental::RResult<void>
ROOT::Experimental::RNTupleModel::RUpdater::AddProjectedField(std::unique_ptr<RFieldBase> field,
                                                              FieldMappingFunc_t mapping)
{
   auto fieldp = field.get();
   auto result = fOpenChangeset.fModel.AddProjectedField(std::move(field), mapping);
   if (result)
      fOpenChangeset.fAddedProjectedFields.emplace_back(fieldp);
   return R__FORWARD_RESULT(result);
}

void ROOT::Experimental::RNTupleModel::EnsureValidFieldName(std::string_view fieldName)
{
   RResult<void> nameValid = RFieldBase::EnsureValidFieldName(fieldName);
   if (!nameValid) {
      nameValid.Throw();
   }
   auto fieldNameStr = std::string(fieldName);
   if (fFieldNames.count(fieldNameStr) > 0)
      throw RException(R__FAIL("field name '" + fieldNameStr + "' already exists in NTuple model"));
}

void ROOT::Experimental::RNTupleModel::EnsureNotFrozen() const
{
   if (IsFrozen())
      throw RException(R__FAIL("invalid attempt to modify frozen model"));
}

void ROOT::Experimental::RNTupleModel::EnsureNotBare() const
{
   if (IsBare())
      throw RException(R__FAIL("invalid attempt to use default entry of bare model"));
}

ROOT::Experimental::RNTupleModel::RNTupleModel(std::unique_ptr<RFieldZero> fieldZero)
   : fFieldZero(std::move(fieldZero)), fModelId(GetNewModelId()), fSchemaId(fModelId)
{}

std::unique_ptr<ROOT::Experimental::RNTupleModel> ROOT::Experimental::RNTupleModel::CreateBare()
{
   return CreateBare(std::make_unique<RFieldZero>());
}

std::unique_ptr<ROOT::Experimental::RNTupleModel>
ROOT::Experimental::RNTupleModel::CreateBare(std::unique_ptr<RFieldZero> fieldZero)
{
   auto model = std::unique_ptr<RNTupleModel>(new RNTupleModel(std::move(fieldZero)));
   model->fProjectedFields = std::make_unique<Internal::RProjectedFields>(model.get());
   return model;
}

std::unique_ptr<ROOT::Experimental::RNTupleModel> ROOT::Experimental::RNTupleModel::Create()
{
   return Create(std::make_unique<RFieldZero>());
}

std::unique_ptr<ROOT::Experimental::RNTupleModel>
ROOT::Experimental::RNTupleModel::Create(std::unique_ptr<RFieldZero> fieldZero)
{
   auto model = CreateBare(std::move(fieldZero));
   model->fDefaultEntry = std::unique_ptr<REntry>(new REntry(model->fModelId, model->fSchemaId));
   return model;
}

std::unique_ptr<ROOT::Experimental::RNTupleModel> ROOT::Experimental::RNTupleModel::Clone() const
{
   auto cloneModel = std::unique_ptr<RNTupleModel>(
      new RNTupleModel(std::unique_ptr<RFieldZero>(static_cast<RFieldZero *>(fFieldZero->Clone("").release()))));
   cloneModel->fModelId = GetNewModelId();
   // For a frozen model, we can keep the schema id because adding new fields is forbidden. It is reset in Unfreeze()
   // if called by the user.
   if (fIsFrozen) {
      cloneModel->fSchemaId = fSchemaId;
   } else {
      cloneModel->fSchemaId = cloneModel->fModelId;
   }
   cloneModel->fIsFrozen = fIsFrozen;
   cloneModel->fFieldNames = fFieldNames;
   cloneModel->fDescription = fDescription;
   cloneModel->fProjectedFields = fProjectedFields->Clone(cloneModel.get());
   if (fDefaultEntry) {
      cloneModel->fDefaultEntry = std::unique_ptr<REntry>(new REntry(cloneModel->fModelId, cloneModel->fSchemaId));
      for (const auto &f : cloneModel->fFieldZero->GetSubFields()) {
         cloneModel->fDefaultEntry->AddValue(f->CreateValue());
      }
   }
   return cloneModel;
}

ROOT::Experimental::RFieldBase *ROOT::Experimental::RNTupleModel::FindField(std::string_view fieldName) const
{
   if (fieldName.empty())
      return nullptr;

   auto *field = static_cast<ROOT::Experimental::RFieldBase *>(fFieldZero.get());
   for (auto subfieldName : ROOT::Split(fieldName, ".")) {
      const auto subfields = field->GetSubFields();
      auto it = std::find_if(subfields.begin(), subfields.end(),
                             [&](const auto *f) { return f->GetFieldName() == subfieldName; });
      if (it != subfields.end()) {
         field = *it;
      } else {
         field = nullptr;
         break;
      }
   }

   return field;
}

void ROOT::Experimental::RNTupleModel::AddField(std::unique_ptr<RFieldBase> field)
{
   EnsureNotFrozen();
   if (!field)
      throw RException(R__FAIL("null field"));
   EnsureValidFieldName(field->GetFieldName());

   if (fDefaultEntry)
      fDefaultEntry->AddValue(field->CreateValue());
   fFieldNames.insert(field->GetFieldName());
   fFieldZero->Attach(std::move(field));
}

ROOT::Experimental::RResult<void>
ROOT::Experimental::RNTupleModel::AddProjectedField(std::unique_ptr<RFieldBase> field, FieldMappingFunc_t mapping)
{
   EnsureNotFrozen();
   if (!field)
      return R__FAIL("null field");
   auto fieldName = field->GetFieldName();

   Internal::RProjectedFields::FieldMap_t fieldMap;
   auto sourceField = FindField(mapping(fieldName));
   if (!sourceField)
      return R__FAIL("no such field: " + mapping(fieldName));
   fieldMap[field.get()] = sourceField;
   for (const auto &subField : *field) {
      sourceField = FindField(mapping(subField.GetQualifiedFieldName()));
      if (!sourceField)
         return R__FAIL("no such field: " + mapping(fieldName));
      fieldMap[&subField] = sourceField;
   }

   EnsureValidFieldName(fieldName);
   auto result = fProjectedFields->Add(std::move(field), fieldMap);
   if (!result) {
      return R__FORWARD_ERROR(result);
   }
   fFieldNames.insert(fieldName);
   return RResult<void>::Success();
}

ROOT::Experimental::RFieldZero &ROOT::Experimental::RNTupleModel::GetFieldZero()
{
   if (!IsFrozen())
      throw RException(R__FAIL("invalid attempt to get mutable zero field of unfrozen model"));
   return *fFieldZero;
}

const ROOT::Experimental::RFieldBase &ROOT::Experimental::RNTupleModel::GetField(std::string_view fieldName) const
{
   auto f = FindField(fieldName);
   if (!f)
      throw RException(R__FAIL("invalid field: " + std::string(fieldName)));

   return *f;
}

ROOT::Experimental::REntry &ROOT::Experimental::RNTupleModel::GetDefaultEntry()
{
   EnsureNotBare();
   return *fDefaultEntry;
}

const ROOT::Experimental::REntry &ROOT::Experimental::RNTupleModel::GetDefaultEntry() const
{
   if (!IsFrozen())
      throw RException(R__FAIL("invalid attempt to get default entry of unfrozen model"));
   EnsureNotBare();
   return *fDefaultEntry;
}

std::unique_ptr<ROOT::Experimental::REntry> ROOT::Experimental::RNTupleModel::CreateEntry() const
{
   if (!IsFrozen())
      throw RException(R__FAIL("invalid attempt to create entry of unfrozen model"));

   auto entry = std::unique_ptr<REntry>(new REntry(fModelId, fSchemaId));
   for (const auto &f : fFieldZero->GetSubFields()) {
      entry->AddValue(f->CreateValue());
   }
   return entry;
}

std::unique_ptr<ROOT::Experimental::REntry> ROOT::Experimental::RNTupleModel::CreateBareEntry() const
{
   if (!IsFrozen())
      throw RException(R__FAIL("invalid attempt to create entry of unfrozen model"));

   auto entry = std::unique_ptr<REntry>(new REntry(fModelId, fSchemaId));
   for (const auto &f : fFieldZero->GetSubFields()) {
      entry->AddValue(f->BindValue(nullptr));
   }
   return entry;
}

ROOT::Experimental::REntry::RFieldToken ROOT::Experimental::RNTupleModel::GetToken(std::string_view fieldName) const
{
   const auto &topLevelFields = fFieldZero->GetSubFields();
   auto it = std::find_if(topLevelFields.begin(), topLevelFields.end(),
                          [&fieldName](const RFieldBase *f) { return f->GetFieldName() == fieldName; });

   if (it == topLevelFields.end()) {
      throw RException(R__FAIL("invalid field name: " + std::string(fieldName)));
   }
   return REntry::RFieldToken(std::distance(topLevelFields.begin(), it), fSchemaId);
}

ROOT::Experimental::RFieldBase::RBulk ROOT::Experimental::RNTupleModel::CreateBulk(std::string_view fieldName) const
{
   if (!IsFrozen())
      throw RException(R__FAIL("invalid attempt to create bulk of unfrozen model"));

   auto f = FindField(fieldName);
   if (!f)
      throw RException(R__FAIL("no such field: " + std::string(fieldName)));
   return f->CreateBulk();
}

void ROOT::Experimental::RNTupleModel::Unfreeze()
{
   if (!IsFrozen())
      return;

   fModelId = GetNewModelId();
   fSchemaId = fModelId;
   if (fDefaultEntry) {
      fDefaultEntry->fModelId = fModelId;
      fDefaultEntry->fSchemaId = fSchemaId;
   }
   fIsFrozen = false;
}

void ROOT::Experimental::RNTupleModel::Freeze()
{
   fIsFrozen = true;
}

void ROOT::Experimental::RNTupleModel::SetDescription(std::string_view description)
{
   EnsureNotFrozen();
   fDescription = std::string(description);
}

std::size_t ROOT::Experimental::RNTupleModel::EstimateWriteMemoryUsage(const RNTupleWriteOptions &options) const
{
   std::size_t bytes = 0;
   std::size_t minPageBufferSize = 0;

   // Start with the size of the page buffers used to fill a persistent sink
   std::size_t nColumns = 0;
   for (auto &&field : *fFieldZero) {
      for (const auto &r : field.GetColumnRepresentatives()) {
         nColumns += r.size();
         for (auto columnType : r) {
            minPageBufferSize +=
               options.GetInitialNElementsPerPage() * Internal::RColumnElementBase::Generate(columnType)->GetSize();
         }
      }
   }
   bytes = std::min(options.GetPageBufferBudget(), nColumns * options.GetMaxUnzippedPageSize());

   // If using buffered writing with RPageSinkBuf, we create a clone of the model and keep at least
   // the compressed pages in memory.
   if (options.GetUseBufferedWrite()) {
      bytes += minPageBufferSize;
      // Use the target cluster size as an estimate for all compressed pages combined.
      bytes += options.GetApproxZippedClusterSize();
      int compression = options.GetCompression();
      if (compression != 0 && options.GetUseImplicitMT() == RNTupleWriteOptions::EImplicitMT::kDefault) {
         // With IMT, compression happens asynchronously which means that the uncompressed pages also stay around. Use a
         // compression factor of 2x as a very rough estimate.
         bytes += 2 * options.GetApproxZippedClusterSize();
      }
   }

   return bytes;
}
