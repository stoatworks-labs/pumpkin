/*
 * Stoatworks Labs - About window data for Pumpkin.
 *
 * A PROVISIONAL hand copy, adapted from langton's, until the backend's
 * stoatworks-backend/scripts/sync-about.py generates this file from the
 * website's projects.json (where Pumpkin is not registered yet). `guide` is
 * empty because no user guide exists, so the About block has no User guide
 * button. Once the sync has run, do not edit: the next sync overwrites it.
 *
 * `version` here is a fallback read from this repo's own manifest at sync
 * time. Anything with a build step injects the real one at build time and
 * overrides this.
 */
#pragma once

namespace stoatworks::about
{
    inline constexpr auto name = "Pumpkin";
    inline constexpr auto slug = "pumpkin";
    inline constexpr auto hook = "A jack-o'-lantern on every LED tile, for Resolume";
    inline constexpr auto licence = "MIT";
    inline constexpr auto guide = "";
    inline constexpr auto page = "https://stoatworks-labs.com/software/pumpkin/";
    inline constexpr auto repo = "https://github.com/stoatworks-labs/pumpkin";
    inline constexpr auto versionFallback = "v0.1.0";

    inline constexpr auto org = "Stoatworks Labs";
    inline constexpr auto home = "https://stoatworks-labs.com";
    inline constexpr auto tagline = "Open tools for the people who run the show.";

    /* The canonical funding links, matching FUNDING.yml and the support footer. */
    struct Link { const char* name; const char* url; };
    inline constexpr Link funding[] = {
        { "GitHub Sponsors", "https://github.com/sponsors/stoatworks-labs" },
        { "Ko-fi", "https://ko-fi.com/stoatworkslabs" },
        { "Patreon", "https://patreon.com/StoatworksLabs" },
        { "Liberapay", "https://liberapay.com/stoatworks-labs" },
    };
}
