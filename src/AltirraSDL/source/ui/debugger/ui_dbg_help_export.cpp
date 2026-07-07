//	AltirraSDL - Debugger help export
//	Portable equivalent of native uidbghelp.cpp.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdalloc.h>
#include "resource.h"
#include "versioninfo.h"
#include "oshelper.h"
#include "ui_file_dialog_sdl3.h"

extern SDL_Window *g_pWindow;

namespace {
	const char kDebugHelpTemplate[] = R"ALTDBGHELP(<!DOCTYPE html>
<html lang="en" xmlns="http://www.w3.org/1999/xhtml">
  <head>
    <meta charset="utf-8" />
    <title>Altirra <!--[VERSION]--> Debugger Help</title>
    <style>
      body {
        font-size: 14px;
        font-family: Verdana, sans-serif;
        overflow: hidden;
        position: absolute;
        box-sizing: border-box;
        width: 100%;
        height: 100%;
        margin: 0;
        padding: 0;
      }

      div {
        box-sizing: border-box;
      }

      .header {
        background: #60a0ff;
        margin: 0;
        padding: 6px;
        border: 0
        font-family: inherit;
        font-size: 20px;
      }

      ul#toc_list {
        padding-left: 10px;
      }

      ul#toc_list li {
        list-style-type: none;
      }

      a {
        text-decoration: none;
      }

      a:hover li {
        background: #9cf;
      }

      a.command_name {
        display: inline-block;
        min-width: 200px;
        margin-right: 16px;
      }

      #container {
        display: flex;
        flex-direction: row;
        width: 100%;
        height: 100%;
        overflow: hidden;
      }

      #split-container {
        flex-grow: 1;
        display: flex;
        flex-direction: column;
        width: 100%;
        height: 100%;
        overflow: hidden;
      }

      #toc-box {
        overflow: auto;
        height: 100%;
        width: 20%;
        resize: horizontal;
      }

      #toc {
        margin: 8px;
      }

      #content-box {
        position: relative;
        flex-grow: 1;
        height: 100%;
        margin-left: 8px;
        padding: 8px;
        border: 0;
      }

      #raw-content {
        white-space: pre;
      }

      #content {
        overflow: auto;
        position: absolute;
        left: 0;
        top: 0;
        width: 100%;
        height: 100%;
        margin: 0;
        padding: 8px;
      }

      #content p.command_desc {
        font-family: monospace;
        white-space: pre-wrap;
        margin-left: 12px;
      }

      #content h1 {
        font-size: 16px;
        font-weight: normal;
        font-family: inherit;
        margin: 40px 0px 8px 0px;

        background: #98c8ff;
        padding: 0.25em 0.25em;
        border: 0;
      }

      #content h1 a.command_name {
        font-weight: bold;
      }

      body {
        background: #f0f0f0;
        color: #000;
      }

      #toc-box {
        border-right: 2px solid #aaa;
      }

      @media (prefers-color-scheme: dark) {
        body {
          background: #181818;
          color: #dcdcdc;
        }

        .header {
          background: #203070;
        }

        #toc-box {
          border-right: 2px solid #222;
        }

        #content h1 {
          background: #202840;
        }

        a:visited {
          color: #a6e;
        }

        a:hover li {
          background: #136;
        }

        a:link {
          color: #4af;
        }
      }
    </style>

    <script>
      function parse_content() {
        raw_content = document.getElementById("raw-content");
        rawtext = raw_content.innerText;
        raw_content.remove();

        content = document.getElementById("content");

        lines = rawtext.split('\n');
        block = [];
        blank_line = false;
        toc_nodes = new Map();
        anchor_id = 0;

        flush = function() {
          if (block.length > 0) {
            p = document.createElement('p');
            p.setAttribute('class', 'command_desc')
            p.innerText = block.join('\n');
            content.appendChild(p);

            block = []
          }
        }

        common_usage_block = []

        for (i = 0; i < lines.length; ++i) {
          line = lines[i].replace(/\r+$;/, '')
          c = line[0];

          if (c == '>' || c == '.') {
            flush();

            if (c == '.') {
              common_usage_block.push(line.replace(/^\.(?: |$)/, ''))
            }
          } else if (c == '^' || c == '+') {
            flush();

            parsed_header = line.match(/^. *(.*?)(?<!,) +(.*)$/);
            commands = parsed_header[1].split(',').map(s => s.trim())

            anchor_name = commands.join('_').replace(/[^_0-9a-z]/gi, '') + '_' + anchor_id;
            ++anchor_id;

            commands.forEach(
              function(cmd) {
                toc_nodes.set(cmd, anchor_name)
              }
            );

            header = document.createElement('h1');
            content.appendChild(header);
            anchor = document.createElement('a');
            header.appendChild(anchor);
            anchor.setAttribute('name', anchor_name);
            anchor.setAttribute('class', "command_name");
            anchor.innerText = parsed_header[1];
            header.appendChild(document.createTextNode(parsed_header[2]));
          } else {
            if (line.trim().length == 0) {
              blank_line = true;
            } else {
              line = line.replace(/^    /, '')

              if (blank_line) {
                if (block.length > 0)
                  block.push('')

                blank_line = false;
              }

              block.push(line)
            }
          }

          document.getElementById('common-usage').innerText = common_usage_block.join('\n')
        }

        get_command_class = function(s) {
          c = s[0];

          if (c == '.') return 2;
          if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return 1;

          return 0;
        }

        toc_keys = Array.from(toc_nodes.keys()).sort(
          function(a, b) {
            ac = get_command_class(a)
            bc = get_command_class(b)

            if (ac != bc) {
              return ac - bc;
            }

            a = a.toLowerCase();
            b = b.toLowerCase();

            return a < b ? -1 : a > b ? 1 : 0;
          }
        );
        toc_list = document.getElementById("toc_list");

        for (i = 0; i < toc_keys.length; ++i) {
          toc_key = toc_keys[i];

          toc_link = document.createElement("a");
          toc_list.appendChild(toc_link);

          toc_node = document.createElement("li");
          toc_link.appendChild(toc_node);

          toc_link.setAttribute("href", "#" + toc_nodes.get(toc_key));
          toc_node.innerText = toc_key;
        }
      }
    </script>
  </head>
  <body>
    <div id="split-container">
      <div class="header">Altirra <!--[VERSION]--> Debugger Help</div>
      <div id="container">
        <div id="toc-box">
          <div id="toc">
            <a href="#common-usage-a">Common Usage</a>
            <p>Commands</p>
            <ul id="toc_list">
            </ul>
          </div>
        </div>
        <div id="content-box">
          <div id="content">
            <p>
              This help is generated from Altirra by the Help &gt; Debugger Help command on the menu. It can also be accessed contextually in the debugger itself using the <tt>.help</tt> command.
            </p>
            <h1><a name="common-usage-a">Common usage</a></h1>
            <p id="common-usage" class="command_desc">
            </p>
            <div id="raw-content"><!--[DBGHELP_CONTENT]--></div>
          </div>
        </div>
      </div>
    </div>
  </body>
  <script>
    parse_content();
  </script>
</html>
)ALTDBGHELP";

	void AppendEscapedHelp(VDStringA& output, const vdfastvector<uint8>& raw) {
		for (uint8 c : raw) {
			if (c == '&')
				output += "&amp;";
			else if (c == '<')
				output += "&lt;";
			else if (c == '>')
				output += "&gt;";
			else
				output += (char)c;
		}
	}

	VDStringA BuildDebugHelpHTML() {
		vdfastvector<uint8> raw;

		if (!ATLoadMiscResource(IDR_DEBUG_HELP, raw))
			throw MyError("Unable to load debugger help resource.");

		VDStringA output;
		const char *src = kDebugHelpTemplate;
		const char *end = src + sizeof(kDebugHelpTemplate) - 1;

		while (src != end) {
			const char *marker = strstr(src, "<!--[");
			if (!marker) {
				output.append(src, end);
				break;
			}

			output.append(src, marker);

			const char *tagStart = marker + 5;
			const char *tagEnd = strstr(tagStart, "]-->");
			if (!tagEnd) {
				output.append(marker, end);
				break;
			}

			const VDStringSpanA tag(tagStart, tagEnd);

			if (tag == "VERSION") {
				output += AT_VERSION;
			} else if (tag == "DBGHELP_CONTENT") {
				AppendEscapedHelp(output, raw);
			}

			src = tagEnd + 4;
		}

		return output;
	}

	void EnsureHtmlExtension(VDStringW& path) {
		const wchar_t *leaf = VDFileSplitPath(path.c_str());
		const wchar_t *ext = VDFileSplitExt(leaf);

		if (!ext || !*ext)
			path += L".html";
	}

	void WriteDebugHelpHTML(VDStringW& path) {
		EnsureHtmlExtension(path);

		const VDStringA output = BuildDebugHelpHTML();

		VDFile f(path.c_str(), nsVDFile::kCreateAlways | nsVDFile::kWrite);
		f.write(output.data(), (sint32)output.size());
		f.close();
	}

	void DebugHelpSaveCallback(void *, const char * const *files, int) {
		if (!files || !files[0])
			return;

		try {
			VDStringW path = VDTextU8ToW(VDStringSpanA(files[0]));
			WriteDebugHelpHTML(path);
		} catch (const VDException& e) {
			fprintf(stderr, "AltirraSDL: debugger help export failed: %ls\n", e.c_str());
		}
	}
}

void ATUIExportDebugHelp() {
	static const SDL_DialogFileFilter kFilters[] = {
		{ "Web page", "html" },
	};

	ATUIShowSaveFileDialog('dbgh', DebugHelpSaveCallback, nullptr,
		g_pWindow, kFilters, 1);
}

bool ATUIExportDebugHelpForTest(const char *utf8Path, VDStringW& outPath) {
	outPath.clear();

	if (!utf8Path || !*utf8Path)
		return true;

	try {
		VDStringW path = VDTextU8ToW(VDStringSpanA(utf8Path));
		WriteDebugHelpHTML(path);
		outPath = path;
		return true;
	} catch (const VDException& e) {
		fprintf(stderr, "AltirraSDL: debugger help export failed: %ls\n", e.c_str());
		return false;
	}
}
