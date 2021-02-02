/*
 * Copyright (c) 2020 The Web eID Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "certificatereader.hpp"

#include "signauthutils.hpp"

using namespace pcsc_cpp;
using namespace electronic_id;

CertificateReader::CertificateReader(const CommandWithArguments& cmd) : CommandHandler(cmd)
{
    validateAndStoreOrigin(cmd.second);
}

void CertificateReader::run(CardInfo::ptr cardInfo)
{
    // TODO: Run card commands in a separate thread and show a (delayed) spinner while they are
    // running?
    static const QMap<ElectronicID::Type, QString> icons {
        {ElectronicID::EstEID, QStringLiteral(":/esteid.png")},
        {ElectronicID::FinEID, QStringLiteral(":/fineid.png")},
        {ElectronicID::LatEID, QStringLiteral(":/lateid.png")},
    };

    const bool isAuthenticate = command.first == CommandType::AUTHENTICATE
        || command.second[QStringLiteral("type")] == QStringLiteral("auth");
    certificateType = isAuthenticate ? CertificateType::AUTHENTICATION : CertificateType::SIGNING;

    const auto certificateBytes = cardInfo->eid().getCertificate(certificateType);
    certificateDer = QByteArray(reinterpret_cast<const char*>(certificateBytes.data()),
                                int(certificateBytes.size()));
    certificate = QSslCertificate(certificateDer, QSsl::Der);
    if (certificate.isNull()) {
        throw electronic_id::Error("Invalid certificate");
    }

    auto certificateStatus = CertificateStatus::VALID;

    if (certificate.isNull()) {
        certificateStatus = CertificateStatus::INVALID;
    }
    if (certificate.effectiveDate() > QDateTime::currentDateTimeUtc()) {
        certificateStatus = CertificateStatus::NOT_YET_ACTIVE;
    }
    if (certificate.expiryDate() < QDateTime::currentDateTimeUtc()) {
        certificateStatus = CertificateStatus::EXPIRED;
    }

    const auto certInfo =
        CertificateInfo {certificateType,
                         icons.value(cardInfo->eid().type()),
                         certificate.subjectInfo(QSslCertificate::CommonName).join(' '),
                         certificate.issuerInfo(QSslCertificate::CommonName).join(' '),
                         certificate.effectiveDate().date().toString(Qt::ISODate),
                         certificate.expiryDate().date().toString(Qt::ISODate),
                         isAuthenticate ? cardInfo->eid().authPinMinMaxLength()
                                        : cardInfo->eid().signingPinMinMaxLength(),
                         isAuthenticate ? cardInfo->eid().authPinRetriesLeft()
                                        : cardInfo->eid().signingPinRetriesLeft()};

    // TODO: check invalid certs, what do they return for subject, issuer etc (probably default
    // values)?
    emit certificateReady(origin.toString(), certificateStatus, certInfo);
}

// TODO: Command handler could also draw the UI now that the UI is refactored from Qt Quick to
// widgets.

void CertificateReader::connectSignals(WebEidUI* window)
{
    connect(this, &CertificateReader::certificateReady, window, &WebEidUI::onCertificateReady);
}

void CertificateReader::validateAndStoreOrigin(const QVariantMap& arguments)
{
    const auto originStr = validateAndGetArgument<QString>(QStringLiteral("origin"), arguments);
    if (originStr.size() > 255) {
        throw std::invalid_argument("origin length cannot exceed 255 characters");
    }

    origin = QUrl(originStr, QUrl::ParsingMode::StrictMode);

    // FIXME: implement argument validation with custom exceptions when adding stdin mode.
    if (!origin.isValid()) {
        throw std::invalid_argument("origin is not a valid URL");
    }
    if (origin.isRelative() || !origin.path().isEmpty() || origin.hasQuery()
        || origin.hasFragment()) {
        throw std::invalid_argument("origin is not in <scheme>://<host>[:<port>] format");
    }
    if (origin.scheme() != QStringLiteral("https") && origin.scheme() != QStringLiteral("wss")) {
        throw std::invalid_argument("origin scheme has to be https or wss");
    }
}