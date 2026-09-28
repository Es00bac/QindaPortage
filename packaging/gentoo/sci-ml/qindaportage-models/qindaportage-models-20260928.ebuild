# Copyright 2026 QindaQt contributors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

# AGENT-NOTE: planner weights for app-portage/qindaportage, in the shape of
# sci-ml/qinda-notes-llm: pinned Hugging Face revision, installed beside the
# other models in /usr/share/gguf where QindaPortage's LlmRuntime scans. On
# BFCL v4 tool calling Qwen3.5-9B scores 66.1 against the 4B's 50.3; the app
# loads it only when MemAvailable covers weights + 2.5 GiB, else the 4B from
# sci-ml/qinda-notes-llm. The version is the date the revision was pinned.
QWEN9_COMMIT="3885219b6810b007914f3a7950a8d1b469d598a5"
QWEN9_FILE="Qwen3.5-9B-UD-Q4_K_XL.gguf"
HF="https://huggingface.co/unsloth/Qwen3.5-9B-GGUF/resolve/${QWEN9_COMMIT}"

DESCRIPTION="Local planner models for QindaPortage (GGUF for llama.cpp)"
HOMEPAGE="https://huggingface.co/Qwen/Qwen3.5-9B"
SRC_URI="qwen3-5-9b? ( ${HF}/${QWEN9_FILE} -> ${PN}-${QWEN9_COMMIT:0:8}-${QWEN9_FILE} )"
S="${WORKDIR}"

LICENSE="Apache-2.0"
SLOT="0"
KEYWORDS="~amd64"
IUSE="+qwen3-5-9b"
REQUIRED_USE="|| ( qwen3-5-9b )"
# Weights: nothing to strip, no ELF files to check, nothing to build.
RESTRICT="binchecks strip"

src_unpack() {
	:
}

# Hard-link the verified distfile into the image when DISTDIR shares a
# filesystem with it (see sci-ml/gguf-coding-models), else copy.
install_model() {
	local src dst="${ED}/usr/share/gguf/${2}"
	src=$(readlink -f "${DISTDIR}/${1}") || die
	mkdir -p "${ED}/usr/share/gguf" || die
	addwrite "${src%/*}"
	if ! ln "${src}" "${dst}" 2>/dev/null; then
		cp "${src}" "${dst}" || die "cannot install ${2}"
	fi
	fowners root:root "/usr/share/gguf/${2}"
	fperms 0644 "/usr/share/gguf/${2}"
}

src_install() {
	use qwen3-5-9b && install_model "${PN}-${QWEN9_COMMIT:0:8}-${QWEN9_FILE}" "${QWEN9_FILE}"
}
