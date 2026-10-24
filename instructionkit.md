**Yes—the earlier answer needed more compositional theory and practical production detail. And the JIANT work should be a selective rework, not a replacement of the instrument:** your fork already contains genre-specific recipes and bass patterns worth refining rather than rebuilding. See [`gen_tables.c`](https://github.com/MarcoCasanova00/JIANT-tekno-rework/blob/main/firmware/src/gen_tables.c).

I reviewed accessible **r/Tekno production discussions**, not a complete subreddit scrape. The useful evidence includes actual sampling workflows and arrangement discussions; mastering prescriptions conflict, so those should not become firmware rules.[1][2][3]

## What the theory should cover

The following are **proposed production and generator-design principles**, not claims that every track in these genres follows them.

### Rhythm is more than density

Distinguish four controls that often get incorrectly bundled into “complexity”:

| Control | What changes | Production exercise |
|---|---|---|
| **Density** | Number of events | Remove every second percussion hit without changing its sound. |
| **Syncopation** | Accents relative to strong beats | Move one accent just before a kick, leaving the kick unchanged. |
| **Articulation** | Gate, accent, slide, envelope | Keep pitches and positions identical; make four different performances. |
| **Cycle length** | When a pattern repeats | Run a shorter percussion loop against the main drum loop. |

Different-length loops sharing the same subdivision produce shifting relationships; that is different from fitting competing subdivisions into the same duration. This is why polymetric sequencing can create development without generating new notes.[4]

**Suggested starting exercise—not a canonical tribe pattern:**

```text
Sixteenths: 1234|1234|1234|1234
Kick:       X...|X...|X...|X...
Bass:       ..X.|..X.|..X.|..X.
Percussion: X..X|..X.|.X..|X...
```

First establish the kick–bass relationship. Then audition the percussion quietly: does it add a second rhythmic interpretation, or merely clutter?

**For JIANT:** separate note density, accent density, articulation variation and mutation rate. A more intense result need not contain more notes.

### Tonal tension needs a center

Modes become useful when the listener can perceive a tonal center; changing the starting point within the same pitch collection changes its interval relationships. Phrygian puts a semitone immediately above the tonic, whereas Aeolian has a whole tone there.[5]

**Suggested writing hierarchy:**

- **Foundation:** establish the root clearly.
- **Stability:** use the octave or fifth to reinforce it.
- **Identity:** introduce a small interval cell rather than a long melody.
- **Tension:** occasionally introduce a neighboring semitone, then return.
- **Development:** alter register, articulation or timbre before adding pitches.

For example, compare these **original exercises**, expressed as semitone offsets:

```text
Stable cell:    0, 0, 7, 12
Tension cell:   0, 1, 0, 7
Minor cell:    0, 3, 7, 3
```

These are alternative vocabularies—not genre definitions. **Do not hard-code “mental = Phrygian,” or “tekno = minor chords.”**

**For JIANT:** allow a tonic-centered or single-harmony result to count as complete. Preserve the richer harmony system for styles and user choices that benefit from it.

## Production techniques worth encoding

### Kick–bass: design the handover

A useful kick model separates **transient, descending body and low-frequency decay**; an unsuitable balance between them can obscure either attack or weight. Compression changes that balance—it is not simply a loudness control.[6]

**Recommended workflow:**

1. Establish the kick’s useful decay with the other tracks muted.
2. Add the bass and adjust its onset, gate and release.
3. Decide whether the bass answers the kick or deliberately overlaps it.
4. Add distortion only after that relationship works.
5. Recheck at matched loudness; louder is not automatically better.

Use tempo-relative timing as a starting point:

\[
t_{\text{sixteenth}}=\frac{15000}{\text{BPM}}\text{ milliseconds}
\]

At **160 BPM**, a sixteenth lasts **93.75 ms**. This is a derived timing reference, **not** a recommended universal decay.

**For JIANT:** distinguish kick–bass relationships such as *answering*, *rolling*, *sustained* and *interlocking*. Do not make “short bass” the sole definition of genre authenticity.

### Distortion: placement is composition

EQ or filtering **before** distortion changes what excites the nonlinear stage; processing afterward shapes the generated spectrum. Those are different operations.[4]

**Suggested patch experiment:** keep the note pattern fixed and compare:

- A short, bright input into moderate drive.
- A darker input into stronger drive.
- A resonant input with restrained drive.
- A cleaner low-frequency foundation beneath a dirtier upper layer, where routing permits.

**For JIANT:** create several genuinely different distortion/envelope relationships rather than several presets distinguished mostly by the drive value.

### Acid: sequence the articulation

Roland’s own sequencer treats pitch, gate, accent and slide as distinct information; cutoff, envelope depth and decay provide further performance controls.[7]

**Suggested acidcore exercise:** begin with three pitches, several rests, a few accents and two connected notes. Make variations by changing **only** gate, accent and slide placement; then perform the filter.

**For JIANT:** test the entire articulation path. Having `SF_ACCENT`, `SF_SLIDE` and `ST_TIE` flags is necessary, but the selected engine must produce the intended response. Those flags already exist in [`core.h`](https://github.com/MarcoCasanova00/JIANT-tekno-rework/blob/main/firmware/src/core.h).

### Mental: evolve a relationship

An r/Tekno hardware discussion specifically describes using FM percussion as an arpeggiated voice with delay and flanging.[8]

**Suggested mental workflow:** establish one recognizable motif; evolve its brightness, modulation depth or spatial treatment while keeping another element stable. Avoid independently randomizing everything on every cycle.

For JIANT, separate:

- **Sound variation:** changing the voice.
- **Phrase variation:** changing its articulation.
- **Structural variation:** introducing or removing it.

These should not all fire together by default.

### Resampling without replacement

One documented r/Tekno workflow combines standard drums, a single-cycle bass and a distorted, resampled acid line.[3]

**Design implication:** familiar source material can remain valuable after different sequencing and treatment. A sound that does not initially suggest tekno is not automatically a deletion candidate.

## Preserve versus change

**“Less used” cannot be established from source code.** Until there is a user-marked shortlist or usage evidence, treat low usage as unknown—not as permission to remove something.

| Preserve | Change selectively | Avoid |
|---|---|---|
| Existing engines and their general-purpose capabilities | Genre-specific sound-selection weights | Retiring engines because their factory demos sound unsuitable |
| Useful drums, basses, bells, noise and textures | Envelope, register and articulation defaults | Equating “melodic,” “lo-fi” or “metallic” with inappropriate |
| Existing presets and access to them | Default prominence in a tekno-focused view | Deleting presets merely to declutter generation |
| Other genre behavior | Tekno-specific harmony and mutation policies | Globally imposing tekno restrictions |
| Saved-project compatibility | Opt-in alternatives and carefully tested additions | Reordering persistent identifiers without migration |

**The least destructive approach is to change what the generator prefers before changing what the instrument contains.**

## What your fork needs next

The following are **static source-audit findings**, not listening-test or hardware-validation results.

### Existing work to retain

Your tables already contain dedicated recipes including `SR_TRIBE_BASS`, `SR_MENTAL_BASS`, `SR_MENTAL`, `SR_ACIDCORE`, `SR_TEKK_BASS` and `SR_TEKK_STAB`, plus corresponding bass archetypes. Keep these as the starting point. [`gen_tables.c`](https://github.com/MarcoCasanova00/JIANT-tekno-rework/blob/main/firmware/src/gen_tables.c)

### Fix behavior before adding presets

| Area | Finding | Narrow next step |
|---|---|---|
| **Envelope handling** | Generic lead/arp shaping can replace the recipe’s ADSR afterward. [`gen.c`](https://github.com/MarcoCasanova00/JIANT-tekno-rework/blob/main/firmware/src/gen.c) | Add an explicit recipe-envelope policy; retain existing generic behavior elsewhere. |
| **Release handling** | Recipe release is subsequently constrained by role-level rules. [`gen.c`](https://github.com/MarcoCasanova00/JIANT-tekno-rework/blob/main/firmware/src/gen.c) | Test the **final parameter values**, not just recipe declarations. |
| **Sound selection** | The mental-bass analog route warrants checking against the bounded candidate shortlist. [`gen.c`](https://github.com/MarcoCasanova00/JIANT-tekno-rework/blob/main/firmware/src/gen.c) | Add a reachability test for intended named presets before expanding the bank. |
| **Global exclusions** | `GEN_NEVER` applies across generator selections rather than being a tekno-only aesthetic policy. [`gen_tables.c`](https://github.com/MarcoCasanova00/JIANT-tekno-rework/blob/main/firmware/src/gen_tables.c) | Do not extend it with tekno preferences; use scoped eligibility or weights. |
| **Musical validation** | Existing tests reward progression and variation in several generator checks. [`gen_test.c`](https://github.com/MarcoCasanova00/JIANT-tekno-rework/blob/main/tests/gen_test.c) | Add genre-aware expectations so deliberate stasis is not mistaken for failure. |

## A reversible implementation order

1. **Freeze a baseline:** preserve the current revision, representative saved projects and your preferred sounds.
2. **Audit final output:** inspect selected engine/preset, final envelopes, register, articulation and role balance—not merely the genre table.
3. **Correct routing and overrides:** make existing tekno recipes reliably produce their intended behavior.
4. **Reweight instead of remove:** make unsuitable material uncommon in tekno generation while leaving it available elsewhere.
5. **Improve musical relationships:** kick–bass handover, accent grammar, restrained pitch vocabulary and controlled mutation.
6. **Audition identical seeds:** compare before/after at matched loudness, including non-tekno genres and old projects.
7. **Only then replace weak content:** use your actual shortlist, retaining a recoverable legacy version.

A useful production lesson from the community is that pattern guidance should describe **roles and adaptable starting points**, not mandatory templates. That is also the right direction for this rework.[1]

Fonti
[1] Tekno Percussion/Timekeeping Cheat Sheets https://www.reddit.com/r/Tekno/comments/1qvngef/tekno_percussiontimekeeping_cheat_sheets/
[2] any advice for mixing/mastering ? : r/Tekno https://www.reddit.com/r/Tekno/comments/1prucs8/any_advice_for_mixingmastering/
[3] Sunday acid tribe on the OG Digitakt https://www.reddit.com/r/Tekno/comments/1nsp4u5/sunday_acid_tribe_on_the_og_digitakt/
[4] Let's get experimental: How to use your gear the 'wrong' way ... https://www.musicradar.com/news/get-experimental
[5] Modes https://learningmusic.ableton.com/advanced-topics/modes.html
[6] Drop It: The Kick in Electronic Music https://www.ableton.com/en/blog/drop-it-kick-electronic-music/
[7] TB-303 - Roland Cloud https://www.rolandcloud.com/getmedia/b3e2da28-1951-467e-bd79-7aae34481570/TB-303-Manual-E.pdf?ext=.pdf
[8] chosing analog for making tribe\mental https://www.reddit.com/r/Tekno/comments/112trb6/chosing_analog_for_making_tribemental/
[9] Mental tekno advice https://www.reddit.com/r/Tekno/comments/1pw3uty/mental_tekno_advice/
[10] Question for Tekno producers https://www.reddit.com/r/Tekno/comments/1lj74cy/question_for_tekno_producers/
[11] How to mental tekno https://www.reddit.com/r/Tekno/comments/1g3s1os/how_to_mental_tekno/
[12] How to make a mental tekno kick like Acidpach https://www.reddit.com/r/Tekno/comments/1ppjwi0/how_to_make_a_mental_tekno_kick_like_acidpach/
[13] Plugins and Max for Live Device https://www.reddit.com/r/Tekno/comments/1gfk1yj/plugins_and_max_for_live_device/
[14] young/new/aspiring producers who want to learn (more) about making Tekno/Tribe/Acid, ask me anything and I will do my best to explain. https://www.reddit.com/r/Tekno/comments/1esqu93/youngnewaspiring_producers_who_want_to_learn_more/
[15] Whats Mental/Tribe and what differs them from normal Tekno? https://www.reddit.com/r/Tekno/comments/g3c2v2/whats_mentaltribe_and_what_differs_them_from/
[16] mental tekno sound design digitone 2 https://www.reddit.com/r/Tekno/comments/1x6qfjq/mental_tekno_sound_design_digitone_2/
[17] Mastering kickbass : r/Tekno https://www.reddit.com/r/Tekno/comments/1h5hyxx/mastering_kickbass/
[18] How do i learn how to create mental tekno à la MÄNTÄ ... https://www.reddit.com/r/Tekno/comments/1n56il1/how_do_i_learn_how_to_create_mental_tekno_%C3%A0_la/
[19] How to make kick like new vorteks's songs ? : r/Tekno https://www.reddit.com/r/Tekno/comments/18ph4nm/how_to_make_kick_like_new_vortekss_songs/
[20] Making mental techno with Korg emx1 : r/Tekno https://www.reddit.com/r/Tekno/comments/1pb80tq/produrre_mental_tekno_con_korg_emx1/
[21] How does Tekno/Tribe sound and not Techno? https://www.reddit.com/r/Tekno/comments/1j8u0k4/how_does_teknotribe_sound_and_not_techno/
[22] What scales and keys for tekno kicks and basses? https://www.reddit.com/r/Tekno/comments/1p5cw8u/what_scales_and_keys_for_tekno_kicks_and_basses/
[23] Setup per mental tekno tribe https://www.reddit.com/r/Tekno/comments/1p6aau3/setup_per_mental_tekno_tribe/
[24] I have been building Hardtekk with KORG devices since 2023 https://www.reddit.com/r/Tekno/comments/1m67cv7/i_have_been_building_hardtekk_with_korg_devices/
[25] Tekno in Germany https://www.reddit.com/r/Tekno/comments/13xvx4i/tekno_in_germany/
[26] Tek Production https://www.reddit.com/r/Tekno/comments/1mufaaw/tek_production/
[27] Which subgenre is this? (3 tracks) https://www.reddit.com/r/Tekno/comments/1c7034m/which_subgenre_is_this_3_tracks/
[28] 🎶 Nouveau site pour les passionnés de vinyles hardtek / tribe / freeparty – Teknoboutik.com https://www.reddit.com/r/Tekno/comments/1kg5x4s/nouveau_site_pour_les_passionn%C3%A9s_de_vinyles/
[29] 💣 [FREE DOWNLOAD] Hardtek | Hardtekk | Frenchcore | Spiral Tribe | Tribecore | Mentalcore | Reggaeton x Hardtek Remix – All my tracks on SoundCloud! 💣 https://www.reddit.com/r/Tekno/comments/1lsql3w/free_download_hardtek_hardtekk_frenchcore_spiral/
[30] UNDERGROUND RAVE DJ SET - HARDTEKNO/ACID/TRIBE/HARDTEK https://www.reddit.com/r/Tekno/comments/1lq1fst/underground_rave_dj_set_hardteknoacidtribehardtek/
[31] What subgenre is this? The new challengers - Floxytek https://www.reddit.com/r/Tekno/comments/kelmt1/what_subgenre_is_this_the_new_challengers_floxytek/
[32] r/Tekno https://www.reddit.com/r/Tekno/
[33] How to structure an hardtek song? : r/Tekno https://www.reddit.com/r/Tekno/comments/1igl41m/how_to_structure_an_hardtek_song/
[34] HardTekk UK? : r/Tekno https://www.reddit.com/r/Tekno/comments/1r1gn6d/hardtekk_uk/
[35] Hardtekk from the KORG ELECTRIBE SAMPLER 2 : r/Tekno https://www.reddit.com/r/Tekno/comments/1oq7wtd/hardtekk_from_the_korg_electribe_sampler_2/
[36] This sub should ban Tekk, Tekkno and hardtekk : r/Tekno https://www.reddit.com/r/Tekno/comments/1m7w3jj/this_sub_should_ban_tekk_tekkno_and_hardtekk/
[37] Tekk Hardtekk Tekno Favorite Mix https://www.reddit.com/r/Tekno/comments/11389uc/tekk_hardtekk_tekno_favorite_mix/
[38] Mastering : r/Tekno https://www.reddit.com/r/Tekno/comments/1huzvqe/mastering/
[39] Learn | Production - Roland Articles https://articles.roland.com/learn/production/page/3/
[40] You searched for 303 - Roland Articles https://articles.roland.com/?s=303
[41] MSP Synthesis Tutorial 5: Frequency Modulation https://docs.cycling74.com/learn/articles/06_synthesischapter05/
[42] Drums | Learn - Roland Articles https://articles.roland.com/learn/drums/page/4/
[43] MSP Tutorial 11: Frequency Modulation https://docs.cycling74.com/legacy/max5/tutorials/msp-tut/mspchapter11.html
[44] Synthesis Tutorial 5: Frequency Modulation - Max 7 Documentation https://docs.cycling74.com/legacy/max7/tutorials/06_synthesischapter05
[45] MSP Synthesis Tutorial 5: Frequency Modulation - Max Documentation https://docs.cycling74.com/legacy/max8/tutorials/06_synthesischapter05
[46] Mastering the TB-303 Sequencer in Roland Cloud https://articles.roland.com/mastering-the-tb-303-sequencer-in-roland-cloud/
[47] Roland Articles | Learn | Synthesizer https://articles.roland.com/learn/synthesizer/page/2/
[48] Blog - Page 3 of 57 https://articles.roland.com/blog/page/3/
[49] Christian Moraga, Author at Roland Articles https://articles.roland.com/author/cmoraga/
[50] Beyond Acid: Pushing the TB-303 into New Sonic Territory https://articles.roland.com/beyond-acid-pushing-the-tb-303-into-new-sonic-territory/
[51] TB-303 Archives - Page 2 of 4 https://articles.roland.com/tag/tb-303/page/2/
[52] MSP Tutorial 11: Frequency Modulation https://docs.cycling74.com/max5/tutorials/msp-tut/mspchapter11.html
[53] Roland Cloud Archives - Page 2 of 6 https://articles.roland.com/tag/roland-cloud/page/2/
[54] Mental tekno ideas / how to https://www.reddit.com/r/Tekno/comments/1kdes7x/mental_tekno_ideas_how_to/
[55] mental tekno 150 bpm live set https://www.reddit.com/r/Tekno/comments/1lvegje/mental_tekno_150_bpm_live_set/
[56] Do you have any recommendations in this style of mental tribe? Thanks In advance 😁 https://www.reddit.com/r/Tekno/comments/1otgmys/do_you_have_any_recommendations_in_this_style_of/
[57] Where to start https://www.reddit.com/r/Tekno/comments/1895lmb/where_to_start/
[58] Mentalcore tracks anyone? https://www.reddit.com/r/Tekno/comments/16uzo6n/mentalcore_tracks_anyone/
[59] Recommendations for Tekno that sounds really tribal and dark? https://www.reddit.com/r/Tekno/comments/urq1h9/recommendations_for_tekno_that_sounds_really/
[60] Tekno talks about music, recovery & manifestations on ... https://www.bellanaija.com/2023/01/tekno-on-afrobeats-intelligence-podcast/
[61] Tekno: albums, songs, concerts https://www.deezer.com/en/artist/1363197
[62] The Innovative Past, Present & Future Of Afrobeats Star Tekno https://uproxx.com/music/tekno-interview-afro-nation-festival/
[63] Tekno: The Prince Who Never Became King - The Naija Way https://thenaijaway.substack.com/p/tekno-the-prince-who-never-became-king
[64] Peppermint by Tekno on Apple Music https://music.apple.com/us/album/_/1701161672?i=1701161676&mt=1&ls=1&at=1000lHKX&itscg=30200&itsct=odsl_m
[65] A history of Afrobeats producers, from boy bands to ballads https://pan-african-music.com/en/history-afrobeats-producers/
[66] Tekno Returns To The Heart Of Afrobeats | Features - Clash Magazine https://www.clashmusic.com/features/tekno-returns-to-the-heart-of-afrobeats/
[67] Techno Gamerz answers 16 questions from fans https://www.redbull.com/in-en/techno-gamerz-answers-fan-questions
[68] I AM DJ Cuppy...global DJ/producer, Afrobeats star, proud Nigerian, and daughter of billionaire Femi Otedola. Ask Me Anything! https://www.reddit.com/r/IAmA/comments/7eb19c/i_am_dj_cuppyglobal_djproducer_afrobeats_star/
[69] Ride My Tribe, by Nesh Mayday https://neshmayday.bandcamp.com/track/ride-my-tribe
[70] User talk:Rex the first https://www.wikiwand.com/en/User_talk:Rex_the_first
[71] looking for advice for tekno track https://www.reddit.com/r/Tekno/comments/1pfsm6x/looking_for_advice_for_tekno_track/
[72] How to get free of the 4 bar loop https://www.reddit.com/r/Tekno/comments/1nqlf6u/how_to_get_free_of_the_4_bar_loop/
[73] can anyone explain to me what mental is? https://www.reddit.com/r/Tekno/comments/1n5kgp6/can_anyone_explain_to_me_what_mental_is/
[74] produce : r/Tekno https://www.reddit.com/r/Tekno/comments/1h1u840/produce/
[75] Everyone who I see making mental, its the same:/ : r/Tekno https://www.reddit.com/r/Tekno/comments/1rkqb7w/everyone_who_i_see_making_mental_its_the_same/
[76] Insane Teknology : r/Tekno https://www.reddit.com/r/Tekno/comments/15vi2e1/insane_teknology/
[77] Hey guys! i made new tekkno track and since i want to get ... https://www.reddit.com/r/Tekno/comments/1g14jbv/hey_guys_i_made_new_tekkno_track_and_since_i_want/
[78] r/Tekno - Hi, just started producing on FL and would love ... https://www.reddit.com/r/Tekno/comments/11p7a93/hi_just_started_producing_on_fl_and_would_love/
[79] Best place to find new tracks and best place to buy. : r/Tekno https://www.reddit.com/r/Tekno/comments/1mcquqi/best_place_to_find_new_tracks_and_best_place_to/
[80] acidcore, tribe, mental samples for gstomper studio : r/Tekno https://www.reddit.com/r/Tekno/comments/1wdy9cz/acidcore_tribe_mental_samples_for_gstomper_studio/
[81] Been practicing for about a week now : r/Tekno https://www.reddit.com/r/Tekno/comments/1inbmex/been_practicing_for_about_a_week_now/
[82] r/Tekno https://www.reddit.com/r/Tekno/hot/
[83] Artist showcase - As'teka Nahuatl : r/Tekno https://www.reddit.com/r/Tekno/comments/12zhi5j/artist_showcase_asteka_nahuatl/
[84] Guys, how many of you would be interested in tutorials ... https://www.reddit.com/r/Tekno/comments/hbh5qg/guys_how_many_of_you_would_be_interested_in/
[85] How do I create the sound starting at the 46-second mark? https://www.reddit.com/r/Tekno/comments/1wg3jc0/to_all_tekno_producers_how_do_i_create_the_sound/
[86] The difference between the subgenres : r/Tekno https://www.reddit.com/r/Tekno/comments/16a3lft/the_difference_between_the_subgenres/
[87] 121 royalty free 909 related drum samples (for producing ... https://www.reddit.com/r/Tekno/comments/1mrm554/121_royalty_free_909_related_drum_samples_for/
[88] Question about differentiating tekno subgenres https://www.reddit.com/r/Tekno/comments/kyr9km/question_about_differentiating_tekno_subgenres/
[89] What's the differences between Hardtek and Tribecore https://www.reddit.com/r/Tekno/comments/couy2b/whats_the_differences_between_hardtek_and/
[90] FM1-tribe : r/Tekno https://www.reddit.com/r/Tekno/comments/1x58a5x/fm1tribe/
[91] Anybody into Tekno production knows where to get good ... https://www.reddit.com/r/Tekno/comments/f1sfwq/anybody_into_tekno_production_knows_where_to_get/
[92] Hardtekk // Verschiedene Kicks selber machen , ohne Korg, free Plugins https://www.youtube.com/watch?v=zW7WOLpJqnU
[93] Roland TB-303 Review: The Accidental Synth That Created an ... https://www.michaelmusco.com/2026/03/roland-tb-303-review.html
[94] Roland TB-303 Software Bass Line review https://www.musicradar.com/reviews/roland-tb-303-software-bass-line
[95] Tutorials und Guides - Hardtekk Hub https://hardtekkhub.de/t/tutorials-und-guides
[96] The TB-303 Acid House Sound: How a Failed Bass Machine Became ... https://thenextradio.net/2026/07/31/tb-303-filter-acid-house-craft/
[97] How the Roland TB‑303 Revolutionized Acid House https://www.youtube.com/watch?v=3M3a8IZ8DtQ
[98] www.musicradar.com · reviews · roland-tb-03-bass-lineRoland TB-03 Bass Line review - MusicRadar https://www.musicradar.com/reviews/roland-tb-03-bass-line
[99] Roland TB-303 - Kenton Electronics https://kentonuk.com/ordering-info/what-kenton-products-do-i-need-to-work-with-my-synth/roland-tb-303/
[100] roland.TB-303.secrets.txt http://machines.hyperreal.org/manufacturers/Roland/TB-303/info/roland.TB-303.secrets.txt
[101] © 2019 Roland Corporation https://www.rolandcloud.com/CMS/getmedia/b3e2da28-1951-467e-bd79-7aae34481570/TB-303-Manual-E.pdf?ext=.pdf
[102] TB-3 | Touch Bassline - Roland https://www.roland.com/us/products/tb-3/
[103] musicproductionwiki.com · articles · how-to-recreateHow to Recreate the 303 Acid Sound (The TB-303 Squelch) https://musicproductionwiki.com/articles/how-to-recreate-the-303-acid-sound
[104] [PDF] New Interface As Easy As 303! - Roland Cloud https://www.rolandcloud.com/getmedia/e06fd607-dc62-4b16-a781-623468778912/TB-303-Graphic-Tour.pdf
[105] Struggling https://forum.ableton.com/viewtopic.php?t=95216
[106] The Mixing Thread https://forum.ableton.com/viewtopic.php?t=86956
[107] How Do you Fix Phasing Issues? https://forum.ableton.com/viewtopic.php?t=196992
[108] Ableton Forum https://forum.ableton.com/viewtopic.php?t=194016
[109] Correcting Phase - Ableton Forum https://forum.ableton.com/viewtopic.php?t=111304
[110] Properly Sidechaining/Sub bass-Kick relationship:Dubstep/Dnb https://forum.ableton.com/viewtopic.php?t=220701
[111] 11 radical rhythm tips and tricks https://www.musicradar.com/tuition/tech/11-radical-rhythm-tips-and-tricks-595092
[112] 29. Live Audio Effect Reference https://www.ableton.com/en/manual/live-audio-effect-reference/
[113] kick and bass how to make them sound massive and punch https://forum.ableton.com/viewtopic.php?t=148094
[114] How to create phat electro house kick drums and bass https://forum.ableton.com/viewtopic.php?t=124120
[115] final mixing https://forum.ableton.com/viewtopic.php?t=133772
[116] How to step sequence polymetric drums using Studio ... https://www.musicradar.com/how-to/how-to-step-sequence-polymetric-drums-using-studio-ones-patterns
