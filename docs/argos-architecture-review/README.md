# Argos Collection — Architecture Review Slides

Visual-only slide assets for the Argos collection pipeline architecture review.

## Files

| Slide | File | Topic |
|-------|------|-------|
| 1 | `01-pipeline-stages.svg` | PipelineStager → Compressor (zlib) → Writer |
| 2 | `02-data-flow.svg` | EntryPoint → PipelineStagerInterface → Ledger → Checkpoint |
| 3 | `03-time-advance.svg` | Automatic send-to-pipeline when sim time advances |
| 4 | `04-checkpointer.svg` | Checkpointer chain, encodeForPipeline, FULL/CARRY/CLOSED |
| 5 | `05-contig-delta-encoding.svg` | Contig container delta actions: SWAP, ARRIVE, DEPART, BOOKENDS |
| 6 | `06-sparse-delta-encoding.svg` | Sparse container delta actions: SWAP, REMOVE |

## Import into PowerPoint

1. Open PowerPoint → **Insert → Pictures → This Device**
2. Select the `.svg` files (PowerPoint 2016+ supports SVG natively)
3. Use **Design → Slide Size → Widescreen (16:9)** for best fit
4. Each SVG is 1200×680–820 px — scale to fill the slide

## Alternative: Google Slides / Keynote

Drag the SVGs directly onto a blank slide. If SVG is unsupported, open the files in a browser, screenshot at 100% zoom, or convert with:

```bash
inkscape 01-pipeline-stages.svg --export-filename=01-pipeline-stages.png -w 1920
```

## Source references

- Pipeline wiring: `include/simdb/apps/argos/ArgosCollector.hpp`
- Stager interface: `PipelineStagerInterface.hpp`, `EntryPoint.hpp`
- Time advance: `ArgosCollector::checkTimeAdvanced_()`
- Checkpointer: `Checkpointer.hpp`, `Checkpoint.hpp`
- Container deltas: `CheckpointDeltas.hpp` (`classifyContigChange`, `classifySparseChange`)
