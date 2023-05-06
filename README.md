# e57parser

Playground for parsing e57 files.

Written completely from scratch, intended to be small and with no dependencies so it is trivial to include in existing projects.

**Work in progress, please do not expect this to work correctly**:
- Only cartesian coords are handled.
- Embedded images are ignored.
- Transforms are ignored.

## usage

```
Usage: e57parsers [options] <filename>.e57

Reads the specified E57-file and performs the operations specified by the
command line options. All options can occur multiple times where that makes
sense.

Options:
  --help                       This help text.
  --info                       Output info about the E57 file contents.
  --loglevel=<uint>            Specifies amount of logging, 0=trace,
                               1=debug, 2=info, 3=warnings, 4=errors,
                               5=silent.
  --pointset=<uint>            Selects which point set to process, defaults
                               to 0.
  --include-invalid=<bool>     If enabled, also output points where
                               InvalidState is set. Defaults to false.
  --output-xml=<filename.xml>  Write the embedded XML to a file.
  --output-pts=<filename.pts>  Write the selected point set to file as pts.
```

## License

This application is available to anybody free of charge, under the terms of the MIT License (see LICENSE).

