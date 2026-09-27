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

	Check(tagger != NIL, String("tagger ").Append(taggerID).Append(" not found"));

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

	Check(tagger != NIL, String("tagger ").Append(taggerID).Append(" not found"));

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
	RoundTripFile("wma-tag", config, SingleValueInfo(), "data/base.wma", "wma");
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
		Track			 result;

		tagger->ParseBuffer(buffer, result);

		CheckValues(result.GetInfo(), INFO_BAND, "Same|Other", "vorbis alias names");

		DeleteTagger(tagger);
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
		Track			 result;

		tagger->ParseBuffer(buffer, result);

		CheckValues(result.GetInfo(), INFO_COMPOSER, "A|B", "id3v23 foreign UTF-16");

		DeleteTagger(tagger);
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
