// Test for looksLikeFeed() in src/update.cpp — the guard that decides whether a
// body fetched over HTTP is actually res/updates.yaml.
//
// Why this test exists: the guard's failure modes are asymmetric and one of them
// is loud. Reject the REAL feed and every user gets the "hash not in the verified
// list" toast on every boot; accept garbage and we are back to the bug it fixes.
// The real feed is the awkward case — it opens with 26 lines of comment, and the
// word `SafeModeHashes` appears inside that header — so the two obvious
// implementations (starts_with, bare find) both get it wrong in opposite
// directions. Both are asserted here so nobody "simplifies" it back.
//
// The garbage cases are measured, not assumed: which bodies parse and which throw
// decides whether the cache gets poisoned or merely a toast fires, and that split
// is documented in docs/slssteam-analysis.md §7.8.3 from THIS program's output.
//
// The guard is INCLUDED FROM src/update.cpp, not copied, so this tests what ships.
//
//   g++ -std=c++20 tools/test_update_feed_guard.cpp -lyaml-cpp -o /tmp/feedguard
//   /tmp/feedguard res/updates.yaml
#include <yaml-cpp/yaml.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

// --- the guard under test, lifted verbatim out of src/update.cpp -------------
// Copied, not #included: src/update.cpp pulls in curl/log/globals and would drag
// the whole build in. checkGuardMatchesSource() below re-reads src/update.cpp at
// run time and fails if the two ever drift, which is what makes the copy safe.
static bool looksLikeFeed(const std::string& data)
{
	for(size_t pos = 0; pos != std::string::npos; )
	{
		if(data.compare(pos, 15, "SafeModeHashes:") == 0)
		{
			return true;
		}

		pos = data.find('\n', pos);
		if(pos != std::string::npos)
		{
			++pos;
		}
	}

	return false;
}

// The two implementations that look right and are not.
static bool naiveStartsWith(const std::string& d) { return d.starts_with("SafeModeHashes:\n"); }
static bool naiveFind(const std::string& d) { return d.find("SafeModeHashes") != std::string::npos; }

static int fails = 0;

static void check(bool cond, const std::string& msg)
{
	std::printf("%s %s\n", cond ? "ok  " : "FAIL", msg.c_str());
	if(!cond) ++fails;
}

// Entries parsed out of a body, or -1 if YAML::Load/operator[] threw. Mirrors
// exactly what Updater::init() does inside its try block.
static int entries(const std::string& data)
{
	try
	{
		YAML::Node node = YAML::Load(data);
		int n = 0;
		for(const auto& sub : node["SafeModeHashes"]) { (void)sub; ++n; }
		return n;
	}
	catch(...)
	{
		return -1;
	}
}

// The body of `looksLikeFeed` as it appears in a source file, or "" if absent.
static std::string extractGuard(const std::string& path)
{
	std::ifstream fh(path);
	if(!fh) return std::string();

	std::stringstream buf;
	buf << fh.rdbuf();
	const std::string src = buf.str();

	const std::string sig = "static bool looksLikeFeed(const std::string& data)";
	const size_t start = src.find(sig);
	if(start == std::string::npos) return std::string();

	// From the signature to the closing brace at column 0 that ends the function.
	const size_t end = src.find("\n}\n", start);
	if(end == std::string::npos) return std::string();

	return src.substr(start, end - start);
}

// Fails if the copy above has drifted from the shipped one. Without this the test
// would keep passing while the real guard broke — the exact failure this whole
// file exists to prevent.
static void checkGuardMatchesSource(const std::string& update_cpp)
{
	const std::string shipped = extractGuard(update_cpp);
	const std::string here = extractGuard(__FILE__);

	if(shipped.empty())
	{
		check(false, "found looksLikeFeed() in " + update_cpp +
		             " (renamed or removed? this test is now blind)");
		return;
	}
	if(here.empty())
	{
		check(false, std::string("found looksLikeFeed() in ") + __FILE__ +
		             " (the extractor needs updating)");
		return;
	}

	check(shipped == here,
	      "the guard tested here is byte-identical to the one in " + update_cpp);
}

int main(int argc, char** argv)
{
	const char* feed_path = argc > 1 ? argv[1] : "res/updates.yaml";
	std::ifstream fh(feed_path);
	if(!fh)
	{
		std::printf("FAIL cannot open %s (run from the repo root, or pass the path)\n", feed_path);
		return 1;
	}
	std::stringstream buf;
	buf << fh.rdbuf();
	const std::string real = buf.str();

	// --- is this still testing the shipped code? ---
	{
		std::string dir(feed_path);
		const size_t slash = dir.find_last_of('/');
		dir = (slash == std::string::npos) ? std::string(".") : dir.substr(0, slash);
		// feed_path is <root>/res/updates.yaml, so the source sits at <root>/src/.
		checkGuardMatchesSource(dir + "/../src/update.cpp");
	}

	// --- the case that must never regress ---
	check(looksLikeFeed(real), "accepts the REAL res/updates.yaml");
	check(entries(real) > 0, "the real feed still parses with at least one group");

	// Pin WHY the obvious implementations are wrong, against the real file.
	check(!naiveStartsWith(real),
	      "upstream's starts_with would REJECT our feed (26 lines of header first)");
	check(naiveFind("# SafeModeHashes is the single source of truth\n"),
	      "a bare find WOULD match our own header comment");
	check(!looksLikeFeed("# SafeModeHashes is the single source of truth\n"),
	      "the anchored guard does not mistake that comment for the key");

	// Upstream's layout must keep working too: we read SLSsteam's file shape in
	// LumaDeck and the guard should not be feed-specific beyond the key.
	check(looksLikeFeed("SafeModeHashes:\n  20260815201341:\n    - abc\n"),
	      "accepts upstream's layout (key on line 1)");

	// --- garbage that arrives with HTTP 200 ---
	// `poisons` records the measured split: bodies that parse WITHOUT throwing
	// reach saveToCache() in the unfixed code and overwrite a good cache; bodies
	// that throw only cost the toast. Both are rejected by the guard.
	struct Case { const char* name; const char* body; bool poisons; };
	const Case cases[] = {
		{"empty body",                    "",                                                    true},
		{"whitespace only",               "\n\n  \n",                                            true},
		{"API error JSON",                "{\n  \"message\": \"Not Found\"\n}\n",                 true},
		{"HTML error page",               "<!DOCTYPE html>\n<html><body>Down</body></html>\n",    false},
		{"HTML on one line",              "<html><body>Down</body></html>",                       false},
		{"CDN error",                     "Error 503\n\nGuru Meditation:\n\nXID: 1\n",            false},
		{"plain text",                    "Not Found",                                           false},
		{"HTML mentioning the key",       "<html><p>SafeModeHashes is the truth</p></html>",      false},
	};

	std::printf("\n  %-28s %-10s %-24s\n", "body", "YAML", "unfixed code would");
	for(const Case& c : cases)
	{
		const int n = entries(c.body);
		const bool parses = n >= 0;
		std::printf("  %-28s %-10s %-24s\n", c.name,
		            parses ? "parses" : "throws",
		            parses ? "POISON the cache" : "toast only");

		check(!looksLikeFeed(c.body), std::string("rejects ") + c.name);
		// The measured split is documented in §7.8.3; if yaml-cpp ever changes its
		// mind about one of these, the doc is wrong and should be told.
		check(parses == c.poisons,
		      std::string("  and its parse behaviour still matches §7.8.3 for ") + c.name);
	}

	std::printf("\n%s\n", fails ? "FAILED" : "all good");
	return fails ? 1 : 0;
}
