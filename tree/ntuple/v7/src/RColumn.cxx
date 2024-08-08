/// \file RColumn.cxx
/// \ingroup NTuple ROOT7
/// \author Jakob Blomer <jblomer@cern.ch>
/// \date 2018-10-04
/// \warning This is part of the ROOT 7 prototype! It will change without notice. It might trigger earthquakes. Feedback
/// is welcome!

/*************************************************************************
 * Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include <ROOT/RColumn.hxx>
#include <ROOT/RNTupleDescriptor.hxx>
#include <ROOT/RPageStorage.hxx>

#include <TError.h>

#include <algorithm>
#include <cassert>
#include <utility>

ROOT::Experimental::Internal::RColumn::RColumn(EColumnType type, std::uint32_t columnIndex,
                                               std::uint16_t representationIndex)
   : fType(type), fIndex(columnIndex), fRepresentationIndex(representationIndex), fTeam({this})
{
   const auto [minBits, maxBits] = RColumnElementBase::GetValidBitRange(type);
   (void)minBits;
   fBitsOnStorage = maxBits;
}

ROOT::Experimental::Internal::RColumn::~RColumn()
{
   if (fHandleSink)
      fPageSink->DropColumn(fHandleSink);
   if (fHandleSource)
      fPageSource->DropColumn(fHandleSource);
}

void ROOT::Experimental::Internal::RColumn::ConnectPageSink(DescriptorId_t fieldId, RPageSink &pageSink,
                                                            NTupleSize_t firstElementIndex)
{
   fPageSink = &pageSink; // the page sink initializes fWritePage on AddColumn
   fFirstElementIndex = firstElementIndex;
   fHandleSink = fPageSink->AddColumn(fieldId, *this);
   fApproxNElementsPerPage = fPageSink->GetWriteOptions().GetApproxUnzippedPageSize() / fElement->GetSize();
   if (fApproxNElementsPerPage < 2)
      throw RException(R__FAIL("page size too small for writing"));
   // We now have 0 < fApproxNElementsPerPage / 2 < fApproxNElementsPerPage

   if (pageSink.GetWriteOptions().GetUseTailPageOptimization()) {
      // Allocate two pages that are larger by 50% to accomodate merging a small tail page.
      fWritePage[0] = fPageSink->ReservePage(fHandleSink, fApproxNElementsPerPage + fApproxNElementsPerPage / 2);
      fWritePage[1] = fPageSink->ReservePage(fHandleSink, fApproxNElementsPerPage + fApproxNElementsPerPage / 2);
   } else {
      // Allocate only a single page; small tail pages will not be merged.
      fWritePage[0] = fPageSink->ReservePage(fHandleSink, fApproxNElementsPerPage);
   }
}

void ROOT::Experimental::Internal::RColumn::ConnectPageSource(DescriptorId_t fieldId, RPageSource &pageSource)
{
   fPageSource = &pageSource;
   fHandleSource = fPageSource->AddColumn(fieldId, *this);
   fNElements = fPageSource->GetNElements(fHandleSource);
   fColumnIdSource = fPageSource->GetColumnId(fHandleSource);
   {
      auto descriptorGuard = fPageSource->GetSharedDescriptorGuard();
      fFirstElementIndex = descriptorGuard->GetColumnDescriptor(fColumnIdSource).GetFirstElementIndex();
   }
}

void ROOT::Experimental::Internal::RColumn::Flush()
{
   auto otherIdx = 1 - fWritePageIdx;
   if (fWritePage[fWritePageIdx].IsEmpty() && fWritePage[otherIdx].IsEmpty())
      return;

   if ((fWritePage[fWritePageIdx].GetNElements() < fApproxNElementsPerPage / 2) && !fWritePage[otherIdx].IsEmpty()) {
      // Small tail page: merge with previously used page
      auto &thisPage = fWritePage[fWritePageIdx];
      R__ASSERT(fWritePage[otherIdx].GetMaxElements() >= fWritePage[otherIdx].GetNElements() + thisPage.GetNElements());
      void *dst = fWritePage[otherIdx].GrowUnchecked(thisPage.GetNElements());
      memcpy(dst, thisPage.GetBuffer(), thisPage.GetNBytes());
      thisPage.Reset(0);
      std::swap(fWritePageIdx, otherIdx);
   }

   R__ASSERT(fWritePage[otherIdx].IsEmpty());
   fPageSink->CommitPage(fHandleSink, fWritePage[fWritePageIdx]);
   fWritePage[fWritePageIdx].Reset(fNElements);
}

void ROOT::Experimental::Internal::RColumn::CommitSuppressed()
{
   fPageSink->CommitSuppressedColumn(fHandleSink);
}

bool ROOT::Experimental::Internal::RColumn::TryMapPage(NTupleSize_t globalIndex)
{
   const auto nTeam = fTeam.size();
   std::size_t iTeam = 1;
   do {
      fReadPageRef = fPageSource->LoadPage(fTeam.at(fLastGoodTeamIdx)->GetHandleSource(), globalIndex);
      if (fReadPageRef.Get().IsValid())
         break;
      fLastGoodTeamIdx = (fLastGoodTeamIdx + 1) % nTeam;
      iTeam++;
   } while (iTeam <= nTeam);

   return fReadPageRef.Get().Contains(globalIndex);
}

bool ROOT::Experimental::Internal::RColumn::TryMapPage(RClusterIndex clusterIndex)
{
   const auto nTeam = fTeam.size();
   std::size_t iTeam = 1;
   do {
      fReadPageRef = fPageSource->LoadPage(fTeam.at(fLastGoodTeamIdx)->GetHandleSource(), clusterIndex);
      if (fReadPageRef.Get().IsValid())
         break;
      fLastGoodTeamIdx = (fLastGoodTeamIdx + 1) % nTeam;
      iTeam++;
   } while (iTeam <= nTeam);

   return fReadPageRef.Get().Contains(clusterIndex);
}

void ROOT::Experimental::Internal::RColumn::MergeTeams(RColumn &other)
{
   // We are working on very small vectors here, so quadratic complexity works
   for (auto *c : other.fTeam) {
      if (std::find(fTeam.begin(), fTeam.end(), c) == fTeam.end())
         fTeam.emplace_back(c);
   }

   for (auto c : fTeam) {
      if (c == this)
         continue;
      c->fTeam = fTeam;
   }
}
