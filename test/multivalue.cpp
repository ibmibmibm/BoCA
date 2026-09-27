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

#include <smooth.h>
#include <smooth/main.h>

#include <boca.h>

#include <stdio.h>

using namespace smooth;
using namespace smooth::IO;

using namespace BoCA;

static Int	 failures    = 0;
static Bool	 writeGolden = False;

static Void Check(Bool condition, const String &description)
{
	if (condition) return;

	failures++;

	printf("FAIL: %s\n", (char *) description);
}

static String Join(const Array<String> &values)
{
	return String::Implode(values, "|");
}

static Void CheckValues(const Info &info, const String &key, const String &expected, const String &context)
{
	String	 actual = Join(info.GetOtherInfoList(key));

	Check(actual == expected, String(context).Append(": ").Append(key).Append(" is \"").Append(actual).Append("\", expected \"").Append(expected).Append("\""));
}

/* Golden files hold the expected tag bytes for single-value input;
 * regenerate them with --write-golden.
 */
static Void CompareGolden(const String &name, const Buffer<UnsignedByte> &buffer)
{
	String	 path = String("data/golden-").Append(name).Append(".bin");

	if (writeGolden)
	{
		OutStream	 out(STREAM_FILE, path, OS_REPLACE);

		out.OutputData(buffer, buffer.Size());

		return;
	}

	InStream		 in(STREAM_FILE, path, IS_READ);
	Buffer<UnsignedByte>	 golden(in.Size());

	in.InputData(golden, golden.Size());

	Bool	 same = (golden.Size() == buffer.Size() && memcmp(golden, buffer, buffer.Size()) == 0);

	Check(same, String("golden ").Append(name).Append(" differs"));
}

static Void ReadFile(const String &path, Buffer<UnsignedByte> &buffer)
{
	InStream	 in(STREAM_FILE, path, IS_READ);

	buffer.Resize(in.Size());

	in.InputData(buffer, buffer.Size());
}

/* Find a top-level MP4/QuickTime atom by four-character code within [start, end).
 */
static Bool FindAtom(const Buffer<UnsignedByte> &buffer, Int start, Int end, const char *fourcc, Int &atomStart, Int &atomSize)
{
	Int	 offset = start;

	while (offset + 8 <= end)
	{
		UnsignedInt32	 size = ( (UnsignedInt32) buffer[offset]      << 24) |
					( (UnsignedInt32) buffer[offset + 1]  << 16) |
					( (UnsignedInt32) buffer[offset + 2]  <<  8) |
					  (UnsignedInt32) buffer[offset + 3];

		if (size < 8 || offset + (Int) size > end) return False;

		if (memcmp(buffer + offset + 4, fourcc, 4) == 0)
		{
			atomStart = offset;
			atomSize  = (Int) size;

			return True;
		}

		offset += size;
	}

	return False;
}

/* mp4v2 stores a modification time in the top-level moov/mvhd atom and
 * updates it on every render, which makes a whole-file golden comparison
 * unstable. Compare only the moov/udta atom, which holds the tags mp4-tag
 * writes, instead.
 */
static Bool ExtractUdta(const Buffer<UnsignedByte> &buffer, Buffer<UnsignedByte> &udta)
{
	Int	 moovStart, moovSize;

	if (!FindAtom(buffer, 0, buffer.Size(), "moov", moovStart, moovSize)) return False;

	Int	 udtaStart, udtaSize;

	if (!FindAtom(buffer, moovStart + 8, moovStart + moovSize, "udta", udtaStart, udtaSize)) return False;

	udta.Resize(udtaSize);

	memcpy(udta, buffer + udtaStart, udtaSize);

	return True;
}

static AS::TaggerComponent *CreateTagger(const String &id, Config *config)
{
	AS::Registry		&boca	= AS::Registry::Get();
	AS::TaggerComponent	*tagger = (AS::TaggerComponent *) boca.CreateComponentByID(id);

	Check(tagger != NIL, String("tagger ").Append(id).Append(" not found"));

	if (tagger != NIL) tagger->SetConfiguration(config);

	return tagger;
}

static Void DeleteTagger(AS::TaggerComponent *tagger)
{
	AS::Registry::Get().DeleteComponent(tagger);
}

/* Render a tag to a buffer, compare or write golden bytes, parse it back.
 */
static Info RoundTripBuffer(const String &taggerID, Config *config, const Info &info, const String &golden)
{
	AS::TaggerComponent	*tagger = CreateTagger(taggerID, config);
	Track			 track;
	Track			 result;

	if (tagger == NIL) return Info();

	track.SetInfo(info);

	Buffer<UnsignedByte>	 buffer;

	tagger->RenderBuffer(buffer, track);

	if (golden != NIL) CompareGolden(golden, buffer);

	tagger->ParseBuffer(buffer, result);

	DeleteTagger(tagger);

	return result.GetInfo();
}

/* Write a tag to a copy of a sample file, compare or write golden bytes, read it back.
 */
static Info RoundTripFile(const String &taggerID, Config *config, const Info &info, const String &sample, const String &golden)
{
	AS::TaggerComponent	*tagger = CreateTagger(taggerID, config);
	Track			 track;
	Track			 result;

	if (tagger == NIL) return Info();

	String	 copy = String("data/tmp-").Append(File(sample).GetFileName());

	File(sample).Copy(copy);

	track.SetInfo(info);

	tagger->RenderStreamInfo(copy, track);

	if (golden != NIL)
	{
		Buffer<UnsignedByte>	 buffer;

		ReadFile(copy, buffer);

		Buffer<UnsignedByte>	 udta;

		if (ExtractUdta(buffer, udta)) CompareGolden(golden, udta);
		else				CompareGolden(golden, buffer);
	}

	tagger->ParseStreamInfo(copy, result);

	File(copy).Delete();

	DeleteTagger(tagger);

	return result.GetInfo();
}

/* Info with one value for each key, for golden files.
 */
static Info SingleValueInfo()
{
	Info	 info;

	info.artist = "Artist";
	info.title  = "Title";
	info.album  = "Album";
	info.track  = 1;

	info.other.Add(String(INFO_COMPOSER).Append(":Composer A"));
	info.other.Add(String(INFO_BAND).Append(":Band A"));
	info.other.Add(String(INFO_CATALOGNUMBER).Append(":CAT-1"));
	info.other.Add(String(INFO_MEDIATYPE).Append(":CD"));
	info.other.Add(String(INFO_MUSICBRAINZ_ARTISTID).Append(":11111111-1111-1111-1111-111111111111"));
	info.other.Add(String(INFO_USERTEXT).Append(":Custom:|:Value"));

	return info;
}

/* Info with several values for some keys.
 */
static Info MultiValueInfo()
{
	Info	 info = SingleValueInfo();

	info.AddOtherInfo(INFO_COMPOSER, "Composer B; with separator");
	info.AddOtherInfo(INFO_BAND, "Band B");
	info.AddOtherInfo(INFO_CATALOGNUMBER, "CAT-2/X");
	info.AddOtherInfo(INFO_MEDIATYPE, "Vinyl");
	info.AddOtherInfo(INFO_MUSICBRAINZ_ARTISTID, "22222222-2222-2222-2222-222222222222");
	info.other.Add(String(INFO_USERTEXT).Append(":Other:|:Second"));

	return info;
}

static Void CheckMultiValues(const Info &info, const String &context)
{
	CheckValues(info, INFO_COMPOSER, "Composer A|Composer B; with separator", context);
	CheckValues(info, INFO_BAND, "Band A|Band B", context);
	CheckValues(info, INFO_CATALOGNUMBER, "CAT-1|CAT-2/X", context);
	CheckValues(info, INFO_MEDIATYPE, "CD|Vinyl", context);
	CheckValues(info, INFO_MUSICBRAINZ_ARTISTID, "11111111-1111-1111-1111-111111111111|22222222-2222-2222-2222-222222222222", context);
}

static Void CheckInfo()
{
	Info	 info;

	info.AddOtherInfo(INFO_COMPOSER, "A");
	info.AddOtherInfo(INFO_COMPOSER, "B; C");
	info.AddOtherInfo(INFO_COMPOSER, "A");
	info.AddOtherInfo(INFO_COMPOSER, NIL);
	info.AddOtherInfo(INFO_BAND, "X/Y");

	CheckValues(info, INFO_COMPOSER, "A|B; C", "AddOtherInfo keeps order, skips repeats and NIL");
	CheckValues(info, INFO_BAND, "X/Y", "AddOtherInfo other key");

	Check(info.GetOtherInfo(INFO_COMPOSER) == "A", "GetOtherInfo returns first value");
	Check(info.HasOtherInfo(INFO_COMPOSER), "HasOtherInfo");

	info.SetOtherInfo(INFO_COMPOSER, "D");

	CheckValues(info, INFO_COMPOSER, "D", "SetOtherInfo replaces all values");
	CheckValues(info, INFO_BAND, "X/Y", "SetOtherInfo keeps other keys");

	info.AddOtherInfo(INFO_COMPOSER, "E");
	info.SetOtherInfo(INFO_COMPOSER, NIL);

	Check(!info.HasOtherInfo(INFO_COMPOSER), "SetOtherInfo NIL removes all values");
	Check(info.other.Length() == 1, "SetOtherInfo NIL adds no empty entry");

	/* SetOtherInfo on an existing key keeps its position relative to another key.
	 */
	Info	 positionInfo;

	positionInfo.AddOtherInfo(INFO_COMPOSER, "A");
	positionInfo.AddOtherInfo(INFO_BAND, "X");

	positionInfo.SetOtherInfo(INFO_COMPOSER, "D");

	Int	 composerIndex = -1;
	Int	 bandIndex     = -1;

	foreach (const String &entry, positionInfo.other)
	{
		if	(entry.StartsWith(String(INFO_COMPOSER).Append(":"))) composerIndex = foreachindex;
		else if (entry.StartsWith(String(INFO_BAND).Append(":")))	bandIndex     = foreachindex;
	}

	Check(composerIndex >= 0 && bandIndex >= 0 && composerIndex < bandIndex, "SetOtherInfo keeps key position relative to another key");

	/* GetOtherInfoKeys lists each key once in order of first appearance.
	 */
	Info		 keysInfo;
	Array<String>	 values;

	values.Add("A");
	values.Add("B");

	keysInfo.AddOtherInfo(INFO_COMPOSER, "A");
	keysInfo.AddOtherInfo(INFO_BAND, "X");
	keysInfo.AddOtherInfo(INFO_COMPOSER, values);

	CheckValues(keysInfo, INFO_COMPOSER, "A|B", "AddOtherInfo with a list skips repeats");

	Check(Join(keysInfo.GetOtherInfoKeys()) == String(INFO_COMPOSER).Append("|").Append(INFO_BAND), "GetOtherInfoKeys lists each key once");
}

static Void CheckTaggers()
{
	Config	*config = Config::Copy();

	/* Single-value golden files.
	 */
	config->SetStringValue("Tags", "ID3v2Encoding", "UTF-16LE");
	RoundTripBuffer("id3v2-tag", config, SingleValueInfo(), "id3v23");

	config->SetStringValue("Tags", "ID3v2Encoding", "UTF-8");
	RoundTripBuffer("id3v2-tag", config, SingleValueInfo(), "id3v24");

	RoundTripBuffer("vorbis-tag", config, SingleValueInfo(), "vorbis");
	RoundTripBuffer("apev2-tag", config, SingleValueInfo(), "apev2");
	RoundTripFile("mp4-tag", config, SingleValueInfo(), "data/base.m4a", "mp4");

#ifdef __WIN32__
	/* No golden bytes for WMA: the Windows Media Format SDK writes the
	 * tag, and its output can differ between Windows versions. The
	 * round trips below check the values.
	 */
	RoundTripFile("wma-tag", config, SingleValueInfo(), "data/base.wma", NIL);
#endif

	/* Vorbis Comment: one field for each value.
	 */
	{
		Info	 result = RoundTripBuffer("vorbis-tag", config, MultiValueInfo(), NIL);

		CheckMultiValues(result, "vorbis");
	}

	/* Vorbis Comment: the same value under two field names gives one value.
	 * The buffer is a comment block: vendor length, vendor, item count,
	 * then length and "NAME=value" for each item, numbers little-endian.
	 */
	{
		static const char	*items[] = { "BAND=Same", "ENSEMBLE=Same", "ORCHESTRA=Other", NIL };

		Buffer<UnsignedByte>	 buffer(4 + 4 + 3 * 4 + 9 + 13 + 15);
		OutStream		 out(STREAM_BUFFER, buffer, buffer.Size());

		out.OutputNumber(0, 4);
		out.OutputNumber(3, 4);

		for (Int i = 0; items[i] != NIL; i++)
		{
			out.OutputNumber(strlen(items[i]), 4);
			out.OutputString(items[i]);
		}

		AS::TaggerComponent	*tagger = CreateTagger("vorbis-tag", config);

		if (tagger != NIL)
		{
			Track			 result;

			tagger->ParseBuffer(buffer, result);

			CheckValues(result.GetInfo(), INFO_BAND, "Same|Other", "vorbis alias names");

			DeleteTagger(tagger);
		}
	}

	/* APEv2: one item, values separated by NUL.
	 */
	{
		Info	 result = RoundTripBuffer("apev2-tag", config, MultiValueInfo(), NIL);

		CheckMultiValues(result, "apev2");
	}

	/* APEv2: numbers and web links keep only the first value.
	 */
	{
		Info	 info = SingleValueInfo();

		info.AddOtherInfo(INFO_BPM, "120");
		info.AddOtherInfo(INFO_BPM, "140");
		info.AddOtherInfo(INFO_WEB_ARTIST, "http://a.example/");
		info.AddOtherInfo(INFO_WEB_ARTIST, "http://b.example/");

		Info	 result = RoundTripBuffer("apev2-tag", config, info, NIL);

		CheckValues(result, INFO_BPM, "120", "apev2 numbers keep first value");
		CheckValues(result, INFO_WEB_ARTIST, "http://a.example/", "apev2 web links keep first value");
	}

	/* MP4: one atom, one data element for each value. MP4 has no atom for
	 * band/orchestra, unlike the other four taggers, so this checks the
	 * same keys as CheckMultiValues except INFO_BAND.
	 */
	{
		Info	 info = MultiValueInfo();

		info.AddOtherInfo(INFO_BPM, "120");
		info.AddOtherInfo(INFO_BPM, "140");

		Info	 result = RoundTripFile("mp4-tag", config, info, "data/base.m4a", NIL);

		CheckValues(result, INFO_COMPOSER, "Composer A|Composer B; with separator", "mp4");
		CheckValues(result, INFO_CATALOGNUMBER, "CAT-1|CAT-2/X", "mp4");
		CheckValues(result, INFO_MEDIATYPE, "CD|Vinyl", "mp4");
		CheckValues(result, INFO_MUSICBRAINZ_ARTISTID, "11111111-1111-1111-1111-111111111111|22222222-2222-2222-2222-222222222222", "mp4");
		CheckValues(result, INFO_BPM, "120", "mp4 number keeps first value");
	}

#ifdef __WIN32__
	/* WMA: one attribute for each value. WMA has no atom for
	 * band/orchestra, unlike the other four taggers, so this checks the
	 * same keys as CheckMultiValues except INFO_BAND.
	 */
	{
		Info	 result = RoundTripFile("wma-tag", config, MultiValueInfo(), "data/base.wma", NIL);

		CheckValues(result, INFO_COMPOSER, "Composer A|Composer B; with separator", "wma");
		CheckValues(result, INFO_CATALOGNUMBER, "CAT-1|CAT-2/X", "wma");
		CheckValues(result, INFO_MEDIATYPE, "CD|Vinyl", "wma");
		CheckValues(result, INFO_MUSICBRAINZ_ARTISTID, "11111111-1111-1111-1111-111111111111|22222222-2222-2222-2222-222222222222", "wma");
	}

	/* WMA: numbers and web links keep only the first value.
	 */
	{
		Info	 info = SingleValueInfo();

		info.AddOtherInfo(INFO_BPM, "120");
		info.AddOtherInfo(INFO_BPM, "140");
		info.AddOtherInfo(INFO_WEB_ARTIST, "http://a.example/");
		info.AddOtherInfo(INFO_WEB_ARTIST, "http://b.example/");

		Info	 result = RoundTripFile("wma-tag", config, info, "data/base.wma", NIL);

		CheckValues(result, INFO_BPM, "120", "wma numbers keep first value");
		CheckValues(result, INFO_WEB_ARTIST, "http://a.example/", "wma web links keep first value");
	}
#endif

	/* RIFF: a composer and a lyricist keep only the first value. RIFF has
	 * no RenderStreamInfo, so this round-trips through RenderBuffer and
	 * ParseBuffer directly instead of a sample file.
	 */
	{
		Info	 info = SingleValueInfo();

		info.AddOtherInfo(INFO_COMPOSER, "Composer B");
		info.AddOtherInfo(INFO_LYRICIST, "Lyricist A");
		info.AddOtherInfo(INFO_LYRICIST, "Lyricist B");

		Info	 result = RoundTripBuffer("riff-tag", config, info, NIL);

		CheckValues(result, INFO_COMPOSER, "Composer A", "riff composer keeps first value");
		CheckValues(result, INFO_LYRICIST, "Lyricist A", "riff lyricist keeps first value");
	}

	/* ID3v2.4: one frame, values separated by NUL. The band with no
	 * album artist moves to the album artist instead of staying band.
	 */
	{
		config->SetStringValue("Tags", "ID3v2Encoding", "UTF-8");

		Info	 result = RoundTripBuffer("id3v2-tag", config, MultiValueInfo(), NIL);

		CheckValues(result, INFO_COMPOSER, "Composer A|Composer B; with separator", "id3v24");
		CheckValues(result, INFO_CATALOGNUMBER, "CAT-1|CAT-2/X", "id3v24");
		CheckValues(result, INFO_MEDIATYPE, "CD|Vinyl", "id3v24");
		CheckValues(result, INFO_MUSICBRAINZ_ARTISTID, "11111111-1111-1111-1111-111111111111|22222222-2222-2222-2222-222222222222", "id3v24");
		CheckValues(result, INFO_ALBUMARTIST, "Band A|Band B", "id3v24 band moves to album artist");
		CheckValues(result, INFO_BAND, "", "id3v24 band moves to album artist");
		CheckValues(result, INFO_USERTEXT, "Custom:|:Value|Other:|:Second", "id3v24 user text");
	}

	/* ID3v2.4: several album artists with no band move to the band frame
	 * on write and back to the album artist on read, since the band and
	 * album artist frames are exchanged whenever one of them is empty.
	 */
	{
		config->SetStringValue("Tags", "ID3v2Encoding", "UTF-8");

		Info	 info = SingleValueInfo();

		info.SetOtherInfo(INFO_BAND, NIL);
		info.AddOtherInfo(INFO_ALBUMARTIST, "AA");
		info.AddOtherInfo(INFO_ALBUMARTIST, "BB");

		Info	 result = RoundTripBuffer("id3v2-tag", config, info, NIL);

		CheckValues(result, INFO_ALBUMARTIST, "AA|BB", "id3v24 several album artists");
	}

	/* ID3v2.4: several values with UTF-16 encoding.
	 */
	{
		config->SetStringValue("Tags", "ID3v2Encoding", "UTF-16BE");

		Info	 result = RoundTripBuffer("id3v2-tag", config, MultiValueInfo(), NIL);

		CheckValues(result, INFO_COMPOSER, "Composer A|Composer B; with separator", "id3v24 UTF-16");
		CheckValues(result, INFO_CATALOGNUMBER, "CAT-1|CAT-2/X", "id3v24 UTF-16");

		config->SetStringValue("Tags", "ID3v2Encoding", "UTF-16LE");
	}

	/* ID3v2.3: one frame, values joined by "/".
	 */
	{
		config->SetStringValue("Tags", "ID3v2Encoding", "UTF-16LE");

		Info	 result = RoundTripBuffer("id3v2-tag", config, MultiValueInfo(), NIL);

		CheckValues(result, INFO_COMPOSER, "Composer A/Composer B; with separator", "id3v23");
		CheckValues(result, INFO_CATALOGNUMBER, "CAT-1/CAT-2/X", "id3v23");
		CheckValues(result, INFO_USERTEXT, "Custom:|:Value|Other:|:Second", "id3v23 user text");
	}

	/* ID3v2: a web link keeps only its first value.
	 */
	{
		config->SetStringValue("Tags", "ID3v2Encoding", "UTF-8");

		Info	 info = SingleValueInfo();

		info.AddOtherInfo(INFO_WEB_ARTIST, "http://a.example/");
		info.AddOtherInfo(INFO_WEB_ARTIST, "http://b.example/");

		Info	 result = RoundTripBuffer("id3v2-tag", config, info, NIL);

		CheckValues(result, INFO_WEB_ARTIST, "http://a.example/", "id3v24 web link");
	}

	/* ID3v2: every producer goes into the involved people frame.
	 */
	{
		Info	 info = SingleValueInfo();

		info.AddOtherInfo(INFO_PRODUCER, "Producer A");
		info.AddOtherInfo(INFO_PRODUCER, "Producer B");

		config->SetStringValue("Tags", "ID3v2Encoding", "UTF-8");

		CheckValues(RoundTripBuffer("id3v2-tag", config, info, NIL), INFO_PRODUCER, "Producer A|Producer B", "id3v24 producers");

		config->SetStringValue("Tags", "ID3v2Encoding", "UTF-16LE");

		CheckValues(RoundTripBuffer("id3v2-tag", config, info, NIL), INFO_PRODUCER, "Producer A|Producer B", "id3v23 producers");
	}

	/* ID3v2.3 UTF-16 frame from another program: NUL-separated values with a BOM each.
	 */
	{
		static const UnsignedByte	 frame[] = { 'T', 'C', 'O', 'M', 0, 0, 0, 15, 0, 0,
							     1, 0xFF, 0xFE, 'A', 0, 0, 0, 0xFF, 0xFE, 'B', 0, 0, 0, 0, 0 };
		static const UnsignedByte	 header[] = { 'I', 'D', '3', 3, 0, 0, 0, 0, 0, sizeof(frame) };

		Buffer<UnsignedByte>	 buffer(sizeof(header) + sizeof(frame));

		memcpy(buffer, header, sizeof(header));
		memcpy(buffer + sizeof(header), frame, sizeof(frame));

		AS::TaggerComponent	*tagger = CreateTagger("id3v2-tag", config);

		if (tagger != NIL)
		{
			Track			 result;

			tagger->ParseBuffer(buffer, result);

			CheckValues(result.GetInfo(), INFO_COMPOSER, "A|B", "id3v23 foreign UTF-16");

			DeleteTagger(tagger);
		}
	}

	/* APEv2: an item whose size does not fit in the tag has no value,
	 * and parsing it does not read or write outside the buffer.
	 */
	{
		Info	 info;

		info.title = "Title";

		AS::TaggerComponent	*tagger = CreateTagger("apev2-tag", config);

		if (tagger != NIL)
		{
			Track			 track;
			Buffer<UnsignedByte>	 buffer;

			track.SetInfo(info);

			tagger->RenderBuffer(buffer, track);

			Int	 keyOffset = -1;

			for (Int i = 0; i + 6 <= buffer.Size(); i++) if (memcmp(buffer + i, "Title", 6) == 0) { keyOffset = i; break; }

			Check(keyOffset >= 8, "apev2 title item not found");

			static const UnsignedInt32	 sizes[] = { 0xFFFFFFFF, 0x7FFFFFFF, 0x10000, 0 };

			for (Int i = 0; keyOffset >= 8 && i < 4; i++)
			{
				Buffer<UnsignedByte>	 corrupt(buffer.Size());
				Track			 result;

				memcpy(corrupt, buffer, buffer.Size());

				for (Int b = 0; b < 4; b++) corrupt[keyOffset - 8 + b] = (sizes[i] >> (8 * b)) & 0xFF;

				tagger->ParseBuffer(corrupt, result);

				Check(result.GetInfo().title == NIL, String("apev2 item size ").Append(Number((Int64) sizes[i]).ToHexString()).Append(" gives no value"));
			}

			DeleteTagger(tagger);
		}
	}

	/* APEv2: parsing stops at an item whose size does not fit in the tag,
	 * so the bytes of its value are not read as more items.
	 */
	{
		Info	 info;

		info.artist = "Artist";
		info.title  = "XXXXXXXXXXXXXXXXX";
		info.album  = "Album";

		AS::TaggerComponent	*tagger = CreateTagger("apev2-tag", config);

		if (tagger != NIL)
		{
			Track			 track;
			Track			 result;
			Buffer<UnsignedByte>	 buffer;

			track.SetInfo(info);

			tagger->RenderBuffer(buffer, track);

			Int	 keyOffset = -1;

			for (Int i = 0; i + 6 <= buffer.Size(); i++) if (memcmp(buffer + i, "Title", 6) == 0) { keyOffset = i; break; }

			Check(keyOffset >= 8, "apev2 title item not found");

			if (keyOffset >= 8)
			{
				/* Make the 17 value bytes a valid item: size 3, flags 0,
				 * key "Genre", value "Bad". Then give the title item a
				 * size that does not fit.
				 */
				static const UnsignedByte	 fakeItem[17] = { 3, 0, 0, 0, 0, 0, 0, 0, 'G', 'e', 'n', 'r', 'e', 0, 'B', 'a', 'd' };

				memcpy(buffer + keyOffset + 6, fakeItem, sizeof(fakeItem));

				for (Int b = 0; b < 4; b++) buffer[keyOffset - 8 + b] = (0x10000 >> (8 * b)) & 0xFF;

				tagger->ParseBuffer(buffer, result);

				Check(result.GetInfo().artist == "Artist", "apev2 item before a bad size is read");
				Check(result.GetInfo().genre == NIL && result.GetInfo().album == NIL, "apev2 parsing stops at a bad item size");
			}

			DeleteTagger(tagger);
		}
	}

	/* APEv2: an unknown item keeps all of its values as user defined
	 * texts.
	 */
	{
		Info	 info;

		info.title = "HappyXCalm";

		AS::TaggerComponent	*tagger = CreateTagger("apev2-tag", config);

		if (tagger != NIL)
		{
			Track			 track;
			Track			 result;
			Buffer<UnsignedByte>	 buffer;

			track.SetInfo(info);

			tagger->RenderBuffer(buffer, track);

			Int	 keyOffset = -1;

			for (Int i = 0; i + 6 <= buffer.Size(); i++) if (memcmp(buffer + i, "Title", 6) == 0) { keyOffset = i; break; }

			Check(keyOffset >= 8, "apev2 title item not found");

			if (keyOffset >= 8)
			{
				/* Rename the item to "MOODS" and separate the two values
				 * with a NUL byte.
				 */
				memcpy(buffer + keyOffset, "MOODS", 5);

				buffer[keyOffset + 6 + 5] = 0;

				tagger->ParseBuffer(buffer, result);

				CheckValues(result.GetInfo(), INFO_USERTEXT, "MOODS:|:Happy|MOODS:|:Calm", "apev2 unknown item values");
			}

			DeleteTagger(tagger);
		}
	}

	/* ID3v2: user defined texts with the same description share one
	 * frame. ID3v2.4 keeps each value, ID3v2.3 joins them with "/".
	 */
	{
		Info	 info = SingleValueInfo();

		info.other.Add(String(INFO_USERTEXT).Append(":Custom:|:Value 2"));

		config->SetStringValue("Tags", "ID3v2Encoding", "UTF-8");

		CheckValues(RoundTripBuffer("id3v2-tag", config, info, NIL), INFO_USERTEXT, "Custom:|:Value|Custom:|:Value 2", "id3v24 user text values");

		config->SetStringValue("Tags", "ID3v2Encoding", "UTF-16LE");

		CheckValues(RoundTripBuffer("id3v2-tag", config, info, NIL), INFO_USERTEXT, "Custom:|:Value/Value 2", "id3v23 user text values");
	}

	/* MP4: the album artist is the artist if the tag has no artist.
	 */
	{
		Info	 info = SingleValueInfo();

		info.artist = NIL;
		info.SetOtherInfo(INFO_ALBUMARTIST, "Album Artist");

		Info	 result = RoundTripFile("mp4-tag", config, info, "data/base.m4a", NIL);

		Check(result.artist == "Album Artist", String("mp4 artist from album artist is \"").Append(result.artist).Append("\""));
	}

	/* ID3v2: the original year frame holds the original year, not the year.
	 */
	{
		Info	 info = SingleValueInfo();

		info.year = 2011;
		info.SetOtherInfo(INFO_ORIG_YEAR, "1973");

		config->SetStringValue("Tags", "ID3v2Encoding", "UTF-8");

		CheckValues(RoundTripBuffer("id3v2-tag", config, info, NIL), INFO_ORIG_YEAR, "1973", "id3v24 original year");

		config->SetStringValue("Tags", "ID3v2Encoding", "UTF-16LE");

		CheckValues(RoundTripBuffer("id3v2-tag", config, info, NIL), INFO_ORIG_YEAR, "1973", "id3v23 original year");
	}

	/* MP4: a freeform item with many values keeps its mean and name.
	 */
	{
		Info	 info = SingleValueInfo();
		String	 expected;

		info.SetOtherInfo(INFO_MUSICBRAINZ_ARTISTID, NIL);

		for (Int i = 0; i < 12; i++)
		{
			String	 value = String("00000000-0000-0000-0000-0000000000").Append(i < 10 ? "0" : "").Append(String::FromInt(i));

			info.AddOtherInfo(INFO_MUSICBRAINZ_ARTISTID, value);

			expected.Append(i > 0 ? "|" : "").Append(value);
		}

		CheckValues(RoundTripFile("mp4-tag", config, info, "data/base.m4a", NIL), INFO_MUSICBRAINZ_ARTISTID, expected, "mp4 freeform item with 12 values");
	}

	/* Values of a tag replace the values that a tag read before gave the
	 * same key; keys that the tag does not have keep their values.
	 */
	{
		static const char	*taggers[] = { "id3v2-tag", "vorbis-tag", "apev2-tag", NIL };

		config->SetStringValue("Tags", "ID3v2Encoding", "UTF-8");

		for (Int i = 0; taggers[i] != NIL; i++)
		{
			Info	 info;

			info.title = "Title";
			info.AddOtherInfo(INFO_COMPOSER, "New");

			AS::TaggerComponent	*tagger = CreateTagger(taggers[i], config);

			if (tagger == NIL) continue;

			Track			 track;
			Buffer<UnsignedByte>	 buffer;

			track.SetInfo(info);

			tagger->RenderBuffer(buffer, track);

			Info	 earlier;

			earlier.AddOtherInfo(INFO_COMPOSER, "Old");
			earlier.AddOtherInfo(INFO_LYRICIST, "Kept");

			Track	 result;

			result.SetInfo(earlier);

			tagger->ParseBuffer(buffer, result);

			CheckValues(result.GetInfo(), INFO_COMPOSER, "New", String(taggers[i]).Append(" replaces values of an earlier tag"));
			CheckValues(result.GetInfo(), INFO_LYRICIST, "Kept", String(taggers[i]).Append(" keeps keys it does not have"));

			DeleteTagger(tagger);
		}
	}

	Config::Free(config);
}

Int smooth::Main()
{
	const Array<String>	&args = GUI::Application::GetArguments();

	if (args.Length() > 0 && args.GetFirst() == "--write-golden") writeGolden = True;

	CheckInfo();
	CheckTaggers();

	if (writeGolden) { printf("Golden files written.\n"); return 0; }

	if (failures == 0) printf("All checks passed.\n");
	else		   printf("%d checks failed.\n", (int) failures);

	return failures == 0 ? 0 : 1;
}
