#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>

#define MAX_PATH 1024

char title[256] = "Untitled";
char author[256] = "Unknown";
char year[64] = "";

void write_file(const char *path, const char *content) {
    FILE *fp = fopen(path, "w");
    if (!fp) {
        perror("fopen");
        exit(1);
    }
    fputs(content, fp);
    fclose(fp);
}

void parse_metadata_line(const char *line) {
    if (strncmp(line, "# Title:", 8) == 0) {
        sscanf(line + 8, " %[^\n]", title);
    } else if (strncmp(line, "## Author:", 10) == 0) {
        sscanf(line + 10, " %[^\n]", author);
    } else if (strncmp(line, "## Year:", 8) == 0) {
        sscanf(line + 8, " %[^\n]", year);
    }
}

void escape_html(const char *in, FILE *out) {
    for (; *in; in++) {
        switch (*in) {
            case '&': fputs("&amp;", out); break;
            case '<': fputs("&lt;", out); break;
            case '>': fputs("&gt;", out); break;
            default: fputc(*in, out); break;
        }
    }
}

void format_line_with_italics(const char *line, FILE *out) {
    int in_em = 0;
    while (*line) {
        if (*line == '_') {
            fputs(in_em ? "</em>" : "<em>", out);
            in_em = !in_em;
        } else {
            escape_html((char[]){*line, 0}, out);
        }
        line++;
    }
}

int is_chapter_marker(const char *line) {
    return line[0] == '*' && isdigit(line[1]) && line[2] == '*';
}

void convert_markdown_to_xhtml(const char *input_path, const char *output_path, const char *toc_path) {
    FILE *in = fopen(input_path, "r");
    FILE *out = fopen(output_path, "w");
    FILE *toc = fopen(toc_path, "w");
    if (!in || !out || !toc) {
        perror("file open");
        exit(1);
    }

    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", out);
    fputs("<!DOCTYPE html>\n<html xmlns=\"http://www.w3.org/1999/xhtml\">\n<head>\n<meta charset=\"UTF-8\" />\n", out);
    fprintf(out, "<title>%s</title>\n</head>\n<body>\n", title);

    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", toc);
    fputs("<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">\n", toc);
    fputs("<head><meta name=\"dtb:uid\" content=\"bookid\"/></head>\n", toc);
    fprintf(toc, "<docTitle><text>%s</text></docTitle>\n<navMap>\n", title);

    char line[2048];
    int chapter_count = 0;
    int play_order = 1;

    while (fgets(line, sizeof(line), in)) {
        parse_metadata_line(line);

        if (strncmp(line, "# ", 2) == 0) {
            fprintf(out, "<h1>");
            format_line_with_italics(line + 2, out);
            fputs("</h1>\n", out);
        } else if (strncmp(line, "## ", 3) == 0) {
            fprintf(out, "<h2>");
            format_line_with_italics(line + 3, out);
            fputs("</h2>\n", out);
        } else if (strncmp(line, "### ", 4) == 0) {
            fprintf(out, "<h3>");
            format_line_with_italics(line + 4, out);
            fputs("</h3>\n", out);
        } else if (is_chapter_marker(line)) {
            chapter_count++;
            char id[32];
            snprintf(id, sizeof(id), "chap%d", chapter_count);
            fprintf(out, "<h2 id=\"%s\">", id);
            format_line_with_italics(line, out);
            fputs("</h2>\n", out);

            fprintf(toc,
                "<navPoint id=\"%s\" playOrder=\"%d\">\n"
                "<navLabel><text>%s</text></navLabel>\n"
                "<content src=\"content.xhtml#%s\"/>\n"
                "</navPoint>\n",
                id, play_order++, line, id);
        } else if (line[0] == '\n') {
            fputs("<br/>\n", out);
        } else {
            fputs("<p>", out);
            format_line_with_italics(line, out);
            fputs("</p>\n", out);
        }
    }

    fputs("</navMap>\n</ncx>\n", toc);
    fputs("</body>\n</html>\n", out);

    fclose(in);
    fclose(out);
    fclose(toc);
}

void make_epub_structure(const char *basename, const char *xhtml_path, const char *ncx_path, const char *epub_output_name) {
    char tmpdir[MAX_PATH];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/%s-epub", basename);
    mkdir(tmpdir, 0755);

    char mimetype_path[MAX_PATH];
    snprintf(mimetype_path, sizeof(mimetype_path), "%s/mimetype", tmpdir);
    write_file(mimetype_path, "application/epub+zip");

    char metainf_path[MAX_PATH];
    snprintf(metainf_path, sizeof(metainf_path), "%s/META-INF", tmpdir);
    mkdir(metainf_path, 0755);

    char container_xml[MAX_PATH];
    snprintf(container_xml, sizeof(container_xml), "%s/container.xml", metainf_path);
    write_file(container_xml,
        "<?xml version=\"1.0\"?>\n"
        "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
        "  <rootfiles>\n"
        "    <rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>\n"
        "  </rootfiles>\n"
        "</container>\n"
    );

    char oebps_path[MAX_PATH];
    snprintf(oebps_path, sizeof(oebps_path), "%s/OEBPS", tmpdir);
    mkdir(oebps_path, 0755);

    char content_xhtml[MAX_PATH];
    snprintf(content_xhtml, sizeof(content_xhtml), "%s/content.xhtml", oebps_path);
    char toc_ncx[MAX_PATH];
    snprintf(toc_ncx, sizeof(toc_ncx), "%s/toc.ncx", oebps_path);

    char command[MAX_PATH];
    snprintf(command, sizeof(command), "cp '%s' '%s' && cp '%s' '%s'", xhtml_path, content_xhtml, ncx_path, toc_ncx);
    system(command);

    char content_opf[MAX_PATH];
    snprintf(content_opf, sizeof(content_opf), "%s/content.opf", oebps_path);

    FILE *opf = fopen(content_opf, "w");
    fprintf(opf,
        "<?xml version=\"1.0\"?>\n"
        "<package xmlns=\"http://www.idpf.org/2007/opf\" unique-identifier=\"bookid\" version=\"2.0\">\n"
        "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
        "<dc:title>%s</dc:title>\n"
        "<dc:creator>%s</dc:creator>\n"
        "<dc:language>en</dc:language>\n"
        "<dc:date>%s</dc:date>\n"
        "</metadata>\n"
        "<manifest>\n"
        "  <item id=\"content\" href=\"content.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
        "  <item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>\n"
        "</manifest>\n"
        "<spine toc=\"ncx\">\n"
        "  <itemref idref=\"content\"/>\n"
        "</spine>\n"
        "</package>\n", title, author, year);
    fclose(opf);

    char zip_cmd[MAX_PATH];
    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));

    snprintf(zip_cmd, sizeof(zip_cmd),
        "cd '%s' && zip -X0 '%s' mimetype && zip -rX9 '%s' META-INF OEBPS && mv '%s' '%s/%s'",
        tmpdir, epub_output_name, epub_output_name, epub_output_name, cwd, epub_output_name);

    system(zip_cmd);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: bm2epub <input.md>\n");
        return 1;
    }

    char *input_path = argv[1];
    char *base = basename(input_path);
    char base_name[MAX_PATH];
    strncpy(base_name, base, MAX_PATH);
    char *dot = strrchr(base_name, '.');
    if (dot) *dot = '\0';

    char xhtml_path[MAX_PATH];
    snprintf(xhtml_path, sizeof(xhtml_path), "/tmp/%s.xhtml", base_name);

    char ncx_path[MAX_PATH];
    snprintf(ncx_path, sizeof(ncx_path), "/tmp/%s.ncx", base_name);

    char epub_filename[MAX_PATH];
    snprintf(epub_filename, sizeof(epub_filename), "%s.epub", base_name);

    convert_markdown_to_xhtml(input_path, xhtml_path, ncx_path);
    make_epub_structure(base_name, xhtml_path, ncx_path, epub_filename);

    printf("Generated: %s\n", epub_filename);
    return 0;
}