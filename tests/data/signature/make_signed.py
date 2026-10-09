# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Writes signed.xisf: a monolithic unit signed as spec §9.5 and §10.5 describe, by an XML signature implementation that
has nothing to do with OpenXISF, signxml (MIT), so that the tests read a signature that real tooling verifies.

The root element has the id XISFRootElement, and a detached Signature element follows it: RSA with SHA-256 over
Canonical XML 1.1 of the root element, which its Reference names as #XISFRootElement, with the X.509 certificate of a
key made for the occasion and thrown away. The image, 37 x 23 gray 8-bit pixels with the values (x + 37y) mod 256, is in
an attached block with its SHA-256 checksum, which the signature covers through the header. The script verifies the
signature of what it wrote before it keeps it, inside a wrapper element, since a signed header has two top-level
elements. Run with Python 3 and signxml (pip install signxml):

    python make_signed.py signed.xisf
"""

import datetime
import hashlib
import struct
import sys

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID
from lxml import etree
from signxml import SignatureConfiguration, XMLSigner, XMLVerifier, methods
from signxml.algorithms import CanonicalizationMethod, DigestAlgorithm, SignatureMethod

WIDTH, HEIGHT = 37, 23
BLOCK_POSITION = 8192
PIXELS = bytes((x + WIDTH * y) % 256 for y in range(HEIGHT) for x in range(WIDTH))


def certificate():
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "OpenXISF test signature")])
    start = datetime.datetime(2026, 10, 5, tzinfo=datetime.timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
            .serial_number(1).not_valid_before(start).not_valid_after(start + datetime.timedelta(days=3650))
            .sign(key, hashes.SHA256()))
    return (key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                              serialization.NoEncryption()),
            cert.public_bytes(serialization.Encoding.PEM))


def root_element():
    # Indented as an encoder writes it: canonicalization keeps the white space, so it is signed too.
    return ('<xisf version="1.0" id="XISFRootElement" xmlns="http://www.pixinsight.com/xisf"\n'
            '   xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"\n'
            '   xsi:schemaLocation="http://www.pixinsight.com/xisf http://pixinsight.com/xisf/xisf-1.0.xsd">\n'
            '   <Metadata>\n'
            '      <Property id="XISF:CreationTime" type="TimePoint" value="2026-10-05T12:00:00Z"/>\n'
            '      <Property id="XISF:CreatorApplication" type="String">make_signed.py 1.0</Property>\n'
            '   </Metadata>\n'
            f'   <Image geometry="{WIDTH}:{HEIGHT}:1" sampleFormat="UInt8" colorSpace="Gray"\n'
            f'      location="attachment:{BLOCK_POSITION}:{len(PIXELS)}"\n'
            f'      checksum="sha256:{hashlib.sha256(PIXELS).hexdigest()}"/>\n'
            '</xisf>')


def verify(root, signature, cert):
    wrapper = etree.fromstring(("<wrapper>" + root + signature + "</wrapper>").encode("utf-8"))
    config = SignatureConfiguration(expect_references=1, signature_methods=[SignatureMethod.RSA_SHA256],
                                    digest_algorithms=[DigestAlgorithm.SHA256])
    XMLVerifier().verify(wrapper, x509_cert=cert, id_attribute="id", expect_config=config)


def main(path):
    key, cert = certificate()
    root = root_element()
    signer = XMLSigner(method=methods.detached, signature_algorithm=SignatureMethod.RSA_SHA256,
                       digest_algorithm=DigestAlgorithm.SHA256,
                       c14n_algorithm=CanonicalizationMethod.CANONICAL_XML_1_1)
    signature = etree.tostring(signer.sign(etree.fromstring(root.encode("utf-8")), key=key, cert=cert,
                                           reference_uri="#XISFRootElement", id_attribute="id"),
                               encoding="unicode")
    verify(root, signature, cert)

    header = ('<?xml version="1.0" encoding="UTF-8"?>\n' + root + "\n" + signature + "\n").encode("utf-8")
    preamble = b"XISF0100" + struct.pack("<I", len(header)) + bytes(4)
    if len(preamble) + len(header) > BLOCK_POSITION:
        sys.exit("the header does not fit before the attached block")
    unit = preamble + header
    with open(path, "wb") as output:
        output.write(unit + bytes(BLOCK_POSITION - len(unit)) + PIXELS)


if __name__ == "__main__":
    main(sys.argv[1])
