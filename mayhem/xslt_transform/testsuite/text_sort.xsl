<?xml version="1.0"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
  <xsl:output method="text"/>
  <xsl:template match="/catalog">
    <xsl:for-each select="book">
      <xsl:sort select="price" data-type="number"/>
      <xsl:value-of select="title"/>=<xsl:value-of select="price"/>;</xsl:for-each>
    total=<xsl:value-of select="sum(book/price)"/>
  </xsl:template>
</xsl:stylesheet>
