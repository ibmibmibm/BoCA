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

#include <boca.h>

BoCA_BEGIN_COMPONENT(MetadataFake)

namespace BoCA
{
	/* A metadata component for tests that overrides every function.
	 */
	class MetadataFake : public CS::MetadataComponent
	{
		public:
			static const String	&GetComponentSpecs();

			Int			 GetQueryKeys() const
			{
				return QUERY_KEY_TOC | QUERY_KEY_ID;
			}

			Int			 Query(const DiscMetadata &known, Array<MetadataMatch> &matches)
			{
				MetadataMatch	 match;

				match.recordID = "fake/1";
				match.title    = String("First for ").Append(known.disc.offsets);

				matches.Add(match);

				match.recordID = "fake/2";
				match.title    = "Second";
				match.fuzzy    = True;

				matches.Add(match);

				return QUERY_RESULT_MULTIPLE;
			}

			Bool			 Read(const String &recordID, DiscMetadata &record)
			{
				record.provider = "fake-metadata";
				record.recordID = recordID;
				record.revision = 7;

				record.disc.album = "Fake album";
				record.disc.AddOtherInfo(INFO_MUSICBRAINZ_ALBUMARTISTID, "id1");
				record.disc.AddOtherInfo(INFO_MUSICBRAINZ_ALBUMARTISTID, "id2");

				Info	 track;

				track.track = 1;
				track.title = "Fake title";

				record.tracks.Add(track);
				record.extensions.Add("KEY=value");

				return recordID == "fake/1";
			}

			String			 NormalizeRecordID(const String &input)
			{
				if (input.StartsWith("fake:")) return input.Tail(input.Length() - 5);

				return NIL;
			}

			Int			 GetSubmitData() const	 { return SUBMIT_DATA_TOC; }
			Int			 GetSubmitMethod() const { return SUBMIT_BY_USER; }

			Bool			 Submit(const DiscMetadata &record)
			{
				return record.recordID == "fake/1";
			}
	};
};

const String &BoCA::MetadataFake::GetComponentSpecs()
{
	static String	 componentSpecs = "			\
								\
	  <?xml version=\"1.0\" encoding=\"UTF-8\"?>		\
	  <component>						\
	    <name>Fake Metadata Provider</name>			\
	    <version>1.0</version>				\
	    <id>fake-metadata</id>				\
	    <type>metadata</type>				\
	  </component>						\
								\
	";

	return componentSpecs;
}

BoCA_DEFINE_METADATA_COMPONENT(MetadataFake)

BoCA_END_COMPONENT(MetadataFake)
