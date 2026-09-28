# Copyright 2026
EAPI=8

inherit cmake

DESCRIPTION="A new tool from an overlay"
HOMEPAGE="https://example.org/newtool"
SRC_URI="https://example.org/${P}.tar.gz"

LICENSE="MIT"
SLOT="0"
KEYWORDS="~amd64"
IUSE="+gui test"
