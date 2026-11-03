<?xml version="1.0"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
  <xsl:output method="html"/>
  <xsl:template name="row"><xsl:param name="t"/><tr><td><xsl:value-of select="$t"/></td></tr></xsl:template>
  <xsl:template match="/">
    <html><body><table>
      <xsl:for-each select="//title"><xsl:call-template name="row"><xsl:with-param name="t" select="concat(position(),':',.)"/></xsl:call-template></xsl:for-each>
      <xsl:choose><xsl:when test="count(//book) &gt; 1">many</xsl:when><xsl:otherwise>few</xsl:otherwise></xsl:choose>
      <xsl:number value="7" format="i"/>
    </table></body></html>
  </xsl:template>
</xsl:stylesheet>
