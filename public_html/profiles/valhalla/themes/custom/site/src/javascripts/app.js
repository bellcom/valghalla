jQuery(function ($) {
  'use strict';

  // Flexy header
  flexy_header.init();

  // Sidr
  $('.slinky-menu')
      .find('ul, li, a')
      .removeClass();

  $('.sidr-toggle--right').sidr({
    name: 'sidr-main',
    side: 'right',
    renaming: false,
    body: '.layout__wrapper',
    source: '.sidr-source-provider'
  });

  // Slinky
  $('.sidr .slinky-menu').slinky({
    title: true,
    label: ''
  });

  // Enable / disable Bootstrap tooltips, based upon touch events
  if (Modernizr.touchevents) {
    $('[data-toggle="tooltip"]').tooltip('hide');
  }
  else {
    $('[data-toggle="tooltip"]').tooltip();
  }

  // Scroll to.
  $('[data-scroll-to]').click(function(event) {
    event.preventDefault();

    var $element = $(this);
    var target = $element.attr('data-scroll-to');
    var $target = $(target);

    // Scroll to target.
    $([document.documentElement, document.body]).animate({
      scrollTop: $target.offset().top
    }, 400, function() {

      // Add to URL.
      window.location.hash = target;
    });
  });

  // Sticky.
  $('.sticky').stickySidebar({
    topSpacing: 30,
    bottomSpacing: 30
  });
});
