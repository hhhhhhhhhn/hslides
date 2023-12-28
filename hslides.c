#include "hlib/hflag.h"
#include "hlib/hfs.h"
#include "hlib/core.h"
#include "hlib/hstring.h"
#include "stdio.h"
#include <assert.h>
#include <stdbool.h>

void process_presentation(FILE* input, FILE* output);
void process_slide(str slide, FILE* output);
void process_line(str line, FILE* output);

i32 main(int argc, char** argv) {
	str* input_path = hflag_str('i', "input", "Input file", STR("-"));
	str* output_path = hflag_str('o', "output", "Output file", STR("-"));

	hflag_parse(&argc, &argv);

	FILE* input_file;
	FILE* output_file;

	if (str_eq(*input_path, STR("-"))) {
		input_file = stdin;
	} else {
		input_file = hfs_open_file(*input_path, true, false);
		if (input_file == NULL) panicf("Could not open file %.*s\n", (int)input_path->len, input_path->data);
	}

	if (str_eq(*output_path, STR("-"))) {
		output_file = stdout;
	} else {
		output_file = hfs_open_file(*output_path, false, true);
		if (output_file == NULL) panicf("Could not open file %.*s\n", (int)output_path->len, output_path->data);
	}

	process_presentation(input_file, output_file);

	hfs_close_file(input_file);
	hfs_close_file(output_file);
}

void process_presentation(FILE* input, FILE* output) {
	strbResult input_result = strb_from_file(input);
	assert(input_result.ok);
	strb input_builder = input_result.builder;
	str input_str = str_from_strb(&input_builder);

	usize slide_number = 1;
	while(input_str.len > 0) {
		str slide = str_split_str(&input_str, STR("\n---\n"));
		fprintf(output, "<div class=\"slide\" id=\"slide%lu\">\n", slide_number);
		process_slide(slide, output);
		fprintf(output, "</div>\n");
		slide_number++;
	}

	strb_free(&input_builder);
}

void process_slide(str slide, FILE* output) {
	u32 global_list_level = 0;
	bool in_paragraph = false;

	bool in_extra_line = false; // An extra (empty) line in the end is needed to close all the tags

	while (slide.len > 0 || in_extra_line) {
		str line = str_split_char(&slide, '\n');
		u32 line_heading_level = 0;
		u32 line_list_level = 0;
		bool line_is_paragraph = false;

		if(str_starts_with(line, STR("#"))) { // This is a heading
			while(str_starts_with(line, STR("#"))) {
				line = str_remove_start(line, STR("#"));
				line_heading_level++;
			}
		}
		else if(str_starts_with(str_trim_left(line), STR("- "))) { // This is a list item
			while(str_starts_with(line, STR(" "))) {
				line = str_remove_start(line, STR(" "));
				line_list_level++;
			}
			line_list_level = line_list_level/2 + 1;
			line = str_remove_start(line, STR("- "));
		}
		else if(str_starts_with(line, STR("```"))) { // This is a code block
			str language = str_remove_start(line, STR("```"));
			fprintf(output, "<pre><code class=\"language-%.*s\">", (int)language.len, language.data);
			while(slide.len > 0) {
				str code_line = str_split_char(&slide, '\n');
				if (str_eq(code_line, STR("```"))) {
					break;
				}
				fprintf(output, "%.*s\n", (int)code_line.len, code_line.data);
			}
			fprintf(output, "</code></pre>\n");
			continue; // Does not need to handle inline markdown
		}
		else if(str_starts_with(str_trim(line), STR("!["))) { // This is an image
			str image_src = str_trim(line);
			str image_alt = str_split_str(&image_src, STR("]("));

			image_src = str_remove_end(image_src, STR(")"));
			image_alt = str_remove_start(image_alt, STR("!["));

			fprintf(output, "<img src=\"%.*s\" alt=\"%.*s\">\n", (int)image_src.len, image_src.data, (int)image_alt.len, image_alt.data);

			continue; // Does not need to handle inline markdown
		}
		else if(str_trim(line).len > 0) {
			line_is_paragraph = true;
		}

		line = str_trim(line);

		// First handle tag closings
		while(global_list_level > line_list_level) {
			fprintf(output, "</ul>\n");
			global_list_level--;
		}
		if (in_paragraph && !line_is_paragraph) {
			fprintf(output, "</p>\n");
			in_paragraph = false;
		}

		// Then tag openings
		if (!in_paragraph && line_is_paragraph) {
			fprintf(output, "<p>\n");
			in_paragraph = true;
		}
		while(global_list_level < line_list_level) {
			fprintf(output, "<ul>\n");
			global_list_level++;
		}

		// Then single line tag openings
		if(line_heading_level > 0) {
			fprintf(output, "<h%u>\n", line_heading_level);
		}
		if (line_list_level > 0) {
			fprintf(output, "<li>\n");
		}

		// Then the actual line
		// TODO: Process inline markdown
		process_line(line, output);

		if(line_heading_level > 0) {
			fprintf(output, "</h%u>\n", line_heading_level);
		}
		// Then single line tag closings
		if (line_list_level > 0) {
			fprintf(output, "</li>\n");
		}

		// Makes sure an extra line empty line is processed in the end
		if(slide.len == 0 && !in_extra_line) {
			in_extra_line = true;
		} else {
			in_extra_line = false;
		}
	}
}

str asterisk_tag_opening[4] = {
	{.data = "invalid", .len = sizeof("invalid")-1},
	{.data = "<i>", .len = sizeof("<i>")-1},
	{.data = "<b>", .len = sizeof("<b>")-1},
	{.data = "<b><i>", .len = sizeof("<b><i>")-1},
};
str asterisk_tag_closing[4] = {
	{.data = "invalid", .len = sizeof("invalid")-1},
	{.data = "</i>", .len = sizeof("</i>")-1},
	{.data = "</b>", .len = sizeof("</b>")-1},
	{.data = "</i></b>", .len = sizeof("</i></b>")-1},
};

void process_line(str line, FILE* output) {
	usize i = 0;
	usize asterisk_level = 0;
	while(i < line.len) {
		if(line.data[i] == '*') {
			usize asterisk_in_a_row = 0;
			while(line.data[i] == '*') {
				asterisk_in_a_row++;
				i++;
			};
			if (asterisk_in_a_row > 3) {
				panicf("Too many asterisks.\nLine: %.*s\n", (int)line.len, line.data);
			}
			if (asterisk_level == 0) { // Opening an asterisk
				asterisk_level = asterisk_in_a_row;
				fprintf(output, "%.*s", (int)asterisk_tag_opening[asterisk_in_a_row].len, asterisk_tag_opening[asterisk_in_a_row].data);
			}
			else { // Closing an asterisk
				if(asterisk_level != asterisk_in_a_row) {
					panicf("Asterisk closing does not match asterisk opening.\nLine: %.*s\n", (int)line.len, line.data);
				}
				asterisk_level = 0;
				fprintf(output, "%.*s", (int)asterisk_tag_closing[asterisk_in_a_row].len, asterisk_tag_closing[asterisk_in_a_row].data);
			}
			continue;
		}
		else if (line.data[i] == '[') { // Link
			// TODO: Refactor
			usize link_text_end = i;
			while(line.data[link_text_end] != ']') {
				link_text_end++;
				if(link_text_end >= line.len) {
					panicf("Link text not closed.\nLine: %.*s\n", (int)line.len, line.data);
				}
			}
			str link_text = str_slice(line, i+1, link_text_end);

			usize link_url_start = link_text_end + 2;
			if (link_url_start >= line.len) {
				panicf("Link url not found.\nLine: %.*s\n", (int)line.len, line.data);
			}
			usize link_url_end = link_url_start;
			while(line.data[link_url_end] != ')') {
				link_url_end++;
				if(link_url_end >= line.len) {
					panicf("Link url not closed.\nLine: %.*s\n", (int)line.len, line.data);
				}
			}

			str link_url = str_slice(line, link_url_start, link_url_end);

			fprintf(output, "<a href=\"%.*s\">%.*s</a>", (int)link_url.len, link_url.data, (int)link_text.len, link_text.data);

			while(i < link_url_end + 1) i++;
			continue;
		}
		else if (line.data[i] == '`') {
			fprintf(output, "<code>");
			i++; // consume "`"
			while(line.data[i] != '`') {
				fprintf(output, "%c", line.data[i]);
				i++;
				if(i >= line.len) {
					panicf("Inline code not closed.\nLine: %.*s\n", (int)line.len, line.data);
				}
			}
			i++; // consume "`"
			fprintf(output, "</code>");
			continue;
		}
		else if(line.data[i] == '\\') {
			i++;
		}
		fprintf(output, "%c", line.data[i]);
		i++;
	}

	if (asterisk_level > 0) {
		panicf("Asterisk was not closed.\nLine: %.*s\n", (int)line.len, line.data);
	}
	fprintf(output, " "); // Space between every line.
}
