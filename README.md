# binliner

**Note: this plugin is developed against Binary Ninja development branch and
won't currently build on stable.**


An expanded version of the Binary Ninja example inlining workflow, developed while
working on a project which makes heavy use of transferring condition flags across
function call boundaries

Current additional features:

* Persistent inlining of functions using Analysis database globals (requires the
  project be saved to a BNDB file).

* Ability to mark a function for inlining in all functions.

* Experimental aggressive rewriting of inlined basic blocks to allow correct
  propagation of conditions across inlined sections.

## Condition rewriting

This plugin has an optional feature (off by default) to detect and attempt to
correct cases where Binary Ninja's analysis is not able to detect that a
conditional branch depends on status flags set in the inlined function.

Compare before for an affected function:

![Screenshot of affected function where flags cannot be determined
without inlining](docs/without-inlining.png)

and after:

![Screenshot of affected function fixed up correctly after inlining and
condition rewriting](docs/inlining-fixed-up.png)

To enable it, turn on the setting in Binary Ninja's settings under
`Workflows > binliner`, named **Rewrite conditions immediately after
inlined functions [experimental]**.

Similar to [changing the workflow](#enabling-the-binlinerworkflow), you will
need to do a reanalysis afterwards.

You may need to use a snippet like the following in the Python console to
enable the setting for an existing analysis database:

```python
s.set_bool('workflows.binliner.refactorConditions', True, view=bv)
```

## Building

[Follow the build instructions supplied with official Vector35 C++ plugins][binja-plugin-armv7-build].
Currently you will need a development branch version of Binary Ninja of at least
**3301**, as this plugin uses the structured logging not yet available in stable.
This plugin has been tested against **3317** as of writing.

## Installing

Copy the produced `libworkflow_binliner.dylib`/`libworkflow_binliner.so`/`workflow_binliner.dll` to your Binary Ninja
plugins dir.

### Enabling the BinlinerWorkflow

You must enable the BinlinerWorkflow to use the inliner plugin:

 * [Follow the steps to enable Workflows for your Binary Ninja install][bn-wf].

 * Open the Binary Ninja settings.

 * Under the Workflows section added when you enabled Workflows, find the
   _Function Workflow_ setting, and choose `BinlinerWorkflow`.

This setting will only apply to new analysis databases opened. To make an
existing BNDB use the inliner, open the database and follow the same steps
but choose the Resource scope in the settings at the top of the Setting pane.
You'll need to trigger a full reanalysis after this setting is changed.

[binja-plugin-armv7-build]: https://github.com/Vector35/arch-armv7/blob/b45883e81fc656e2274e3ed48b9a8f3839b5e9b2/README.md#building
[bn-wf]: https://docs.binary.ninja/dev/workflows.html#getting-started