// Live lookups on hwivc.org (blogs, projects, events), so Petra always knows
// the latest posts without changing her prompt. Results are plain text for the AI.
#pragma once
#include <string>

// kind: "blog", "projects", "events" or "all". Newest first, from the sitemap.
std::string HwivcList(const std::string& kind);
// topic: a slug, title or a few keywords. Returns the best-matching page's text.
std::string HwivcRead(const std::string& topic);
