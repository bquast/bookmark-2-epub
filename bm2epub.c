#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

#define MAX_PATH 1024

void write_file(const char *path, const char *content) {
    FILE *fp = fopen(path, "w");
    if (!fp) {
        perror("fopen");
        exit(1);
    }
    fputs(content, fp);
    fclose(fp);
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

void convert_markdown_to_xhtml(const char *input_path, const char *output_path) {
    FILE *in = fopen(input_path, "r");
    if (!in) {
        perror("fopen input");
        exit(1);
    }

    FILE *out = fopen(output_path, "w");
    if (!out) {
        perror("fopen output");
        exit(1);
    }

    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", out);
    fputs("<!DOCTYPE html>\n<html xmlns=\"http://www.w3.org/1999/xhtml\">\n<head>\n<meta charset=\"UTF-8\" />\n<title>Book</title>\n</head>\n<body>\n", out);

    char line[2048];
    while (fgets(line, sizeof(line), in)) {
        if (strncmp(line, "# ", 2) == 0) {
            fprintf(out, "<h1>");
            escape_html(line + 2, out);
            fputs("</h1>\n", out);
        } else if (strncmp(line, "## ", 3) == 0) {
            fprintf(out, "<h2>");
            escape_html(line + 3, out);
            fputs("</h2>\n", out);
        } else if (strncmp(line, "### ", 4) == 0) {
            fprintf(out, "<h3>");
            escape_html(line + 4, out);
            fputs("</h3>\n", out);
        } else if (line[0] == '*' && line[1] >= '0' && line[1] <= '9') {
            fprintf(out, "<h4>");
            escape_html(line, out);
            fputs("</h4>\n", out);
        } else if (line[0] == '\n') {
            fputs("<br/>\n", out);
        } else {
            fputs("<p>", out);
            escape_html(line, out);
            fputs("</p>\n", out);
        }
    }

    fputs("</body>\n</html>\n", out);
    fclose(in);
    fclose(out);
}

void make_epub_structure(const char *basename, const char *xhtml_path, const char *epub_output_name) {
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
    char command[MAX_PATH];
    snprintf(command, sizeof(command), "cp '%s' '%s'", xhtml_path, content_xhtml);
    system(command);

    char content_opf[MAX_PATH];
    snprintf(content_opf, sizeof(content_opf), "%s/content.opf", oebps_path);
    write_file(content_opf,
        "<?xml version=\"1.0\"?>\n"
        "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"bookid\">\n"
        "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
        "<dc:title>Bookmark Book</dc:title>\n"
        "<dc:language>en</dc:language>\n"
        "</metadata>\n"
        "<manifest>\n"
        "  <item id=\"content\" href=\"content.xhtml\" media-type=\"application/xhtml+xml\"/>\n"
        "</manifest>\n"
        "<spine>\n"
        "  <itemref idref=\"content\"/>\n"
        "</spine>\n"
        "</package>\n"
    );

    // zip from tmpdir and move final epub to working directory
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

    char epub_filename[MAX_PATH];
    snprintf(epub_filename, sizeof(epub_filename), "%s.epub", base_name);

    convert_markdown_to_xhtml(input_path, xhtml_path);
    make_epub_structure(base_name, xhtml_path, epub_filename);

    printf("Generated: %s\n", epub_filename);
    return 0;
}