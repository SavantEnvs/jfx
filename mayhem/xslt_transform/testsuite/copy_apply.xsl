<?xml version="1.0"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
  <xsl:output method="xml" indent="yes"/>
  <xsl:key name="by-id" match="book" use="@id"/>
  <xsl:variable name="v" select="count(//book)"/>
  <xsl:template match="@*|node()"><xsl:copy><xsl:apply-templates select="@*|node()"/></xsl:copy></xsl:template>
  <xsl:template match="price"><price cur="usd"><xsl:value-of select="format-number(. * 1.5, '#.00')"/></price></xsl:template>
  <xsl:template match="book[key('by-id','b2')]"><cheap n="{$v}"><xsl:apply-templates/></cheap></xsl:template>
</xsl:stylesheet>
