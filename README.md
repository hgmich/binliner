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
You'll need to save and reopen the database after this before the change
takes effect.

[binja-plugin-armv7-build]: https://github.com/Vector35/arch-armv7/blob/b45883e81fc656e2274e3ed48b9a8f3839b5e9b2/README.md#building
[bn-wf]: https://docs.binary.ninja/dev/workflows.html#getting-started