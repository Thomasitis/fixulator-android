package me.timschneeberger.rootlessjamesdsp.setup

import android.content.Context
import android.os.Build
import io.github.muntashirakon.adb.AbsAdbConnectionManager
import org.bouncycastle.asn1.x500.X500Name
import org.bouncycastle.cert.jcajce.JcaX509CertificateConverter
import org.bouncycastle.cert.jcajce.JcaX509v3CertificateBuilder
import org.bouncycastle.operator.jcajce.JcaContentSignerBuilder
import java.io.File
import java.math.BigInteger
import java.security.KeyFactory
import java.security.KeyPairGenerator
import java.security.PrivateKey
import java.security.cert.Certificate
import java.security.cert.CertificateFactory
import java.security.spec.PKCS8EncodedKeySpec
import java.util.Date

/**
 * Talks to the phone's own ADB daemon over Wireless debugging, so Fixulator can grant itself
 * the one-time permissions (DUMP, PROJECT_MEDIA) without Shizuku or a computer.
 * The key pair is created once and kept in the app's private files.
 */
class FxAdbManager private constructor(context: Context) : AbsAdbConnectionManager() {
    private val privateKey: PrivateKey
    private val certificate: Certificate

    init {
        api = Build.VERSION.SDK_INT
        val dir = File(context.filesDir, "adb").apply { mkdirs() }
        val keyFile = File(dir, "key.pk8")
        val certFile = File(dir, "cert.der")
        if (keyFile.exists() && certFile.exists()) {
            privateKey = KeyFactory.getInstance("RSA").generatePrivate(PKCS8EncodedKeySpec(keyFile.readBytes()))
            certificate = CertificateFactory.getInstance("X.509").generateCertificate(certFile.inputStream())
        } else {
            val gen = KeyPairGenerator.getInstance("RSA").apply { initialize(2048) }
            val kp = gen.generateKeyPair()
            val name = X500Name("CN=Fixulator")
            val now = System.currentTimeMillis()
            val builder = JcaX509v3CertificateBuilder(
                name, BigInteger.valueOf(now), Date(now - 86_400_000L), Date(now + 36_500L * 86_400_000L), name, kp.public)
            val signer = JcaContentSignerBuilder("SHA256withRSA").build(kp.private)
            val cert = JcaX509CertificateConverter().getCertificate(builder.build(signer))
            privateKey = kp.private
            certificate = cert
            keyFile.writeBytes(kp.private.encoded)
            certFile.writeBytes(cert.encoded)
        }
    }

    override fun getPrivateKey(): PrivateKey = privateKey
    override fun getCertificate(): Certificate = certificate
    override fun getDeviceName(): String = "Fixulator"

    /** Run one shell command line and return its output. */
    fun shell(command: String): String {
        val stream = openStream("shell:$command")
        val out = stream.openInputStream().bufferedReader().readText()
        stream.close()
        return out
    }

    companion object {
        @Volatile private var instance: FxAdbManager? = null
        fun get(context: Context): FxAdbManager =
            instance ?: synchronized(this) { instance ?: FxAdbManager(context.applicationContext).also { instance = it } }
    }
}
