 /* BoCA - BonkEnc Component Architecture
  * Copyright (C) 2007-2026 Robert Kausch <robert.kausch@freac.org>
  *
  * This program is free software; you can redistribute it and/or
  * modify it under the terms of the GNU General Public License as
  * published by the Free Software Foundation, either version 2 of
  * the License, or (at your option) any later version.
  *
  * THIS PACKAGE IS PROVIDED "AS IS" AND WITHOUT ANY EXPRESS OR
  * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
  * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE. */

#include <boca/common/metadata/discmetadata.h>

BoCA::DiscMetadata::DiscMetadata()
{
	revision = -1;
}

BoCA::DiscMetadata::DiscMetadata(const DiscMetadata &oRecord)
{
	*this = oRecord;
}

BoCA::DiscMetadata::~DiscMetadata()
{
}

BoCA::DiscMetadata &BoCA::DiscMetadata::operator =(const DiscMetadata &oRecord)
{
	if (&oRecord == this) return *this;

	provider = oRecord.provider;
	recordID = oRecord.recordID;
	revision = oRecord.revision;

	disc	 = oRecord.disc;

	tracks.RemoveAll();
	extensions.RemoveAll();

	foreach (const Info &track, oRecord.tracks) tracks.Add(track);
	foreach (const String &extension, oRecord.extensions) extensions.Add(extension);

	return *this;
}

/* The freedb disc ID: the sum of the digits of each track start in
 * seconds, the disc length in seconds and the number of tracks. Track
 * starts count the two seconds of lead-in that offsets strings include.
 */
static Int SumOfDigits(Int n)
{
	Int	 sum = 0;

	while (n > 0)
	{
		sum += n % 10;
		n   /= 10;
	}

	return sum;
}

static const Int	 FramesPerSecond = 75;
static const Int	 LeadInFrames	 = 150;

UnsignedInt32 BoCA::DiscMetadata::GetFreedbDiscID(const MCDI &mcdi)
{
	Int	 numTocEntries = mcdi.GetNumberOfEntries();
	Int	 n	       = 0;

	for (Int i = 0; i < numTocEntries; i++) n += SumOfDigits((mcdi.GetNthEntryOffset(i) + LeadInFrames) / FramesPerSecond);

	Int	 t = mcdi.GetNthEntryOffset(numTocEntries) / FramesPerSecond - mcdi.GetNthEntryOffset(0) / FramesPerSecond;

	return ((n % 0xff) << 24 | t << 8 | numTocEntries);
}

UnsignedInt32 BoCA::DiscMetadata::GetFreedbDiscID(const String &offsets)
{
	const Array<String>	&values = offsets.Explode("+");

	if (values.Length() < 3) return 0;

	Int	 n = 0;

	for (Int i = 1; i < values.Length() - 1; i++) n += SumOfDigits((Int64) Number::FromHexString(values.GetNth(i)) / FramesPerSecond);

	Int	 t = (Int64) Number::FromHexString(values.GetLast()) / FramesPerSecond - (Int64) Number::FromHexString(values.GetNth(1)) / FramesPerSecond;

	return ((n % 0xff) << 24 | t << 8 | (values.Length() - 2));
}

String BoCA::DiscMetadata::FreedbDiscIDToString(UnsignedInt32 discID)
{
	return Number((Int64) discID).ToHexString(8);
}
